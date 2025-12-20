#include "api/handlers/hook_handler.hpp"
#include "api/handlers/stream_handler.hpp"
#include "api/utils/response_helper.hpp"
#include "streaming/stream_manager.hpp"
#include "gateway/utils/protocol_constants.hpp"
#include "config/constants.hpp"
#include "utils/logger.hpp"
#include <nlohmann/json.hpp>
#include <sstream>
#include <algorithm>

using json = nlohmann::json;

namespace api {
namespace handlers {

HookHandler::HookHandler(std::shared_ptr<streaming::StreamManager> stream_manager,
                        std::shared_ptr<StreamHandler> stream_handler)
    : stream_manager_(stream_manager), stream_handler_(stream_handler), retry_running_(true) {
    if (!stream_manager_) {
        throw std::invalid_argument("StreamManager cannot be null");
    }
    
    // 启动重试线程
    retry_thread_ = std::thread(&HookHandler::RetryThreadLoop, this);
    LOG_INFO("[Hook] Hook Handler 初始化完成，重试机制已启动");
    if (stream_handler_) {
        LOG_INFO("[Hook] 按需拉流功能已启用");
    }
}

HookHandler::~HookHandler() {
    // 停止重试线程
    retry_running_ = false;
    if (retry_thread_.joinable()) {
        retry_thread_.join();
    }
    LOG_INFO("[Hook] Hook Handler 已销毁");
}

void HookHandler::HandleStreamChanged(const httplib::Request& req, httplib::Response& res) {
    auto start_time = std::chrono::steady_clock::now();
    
    try {
        std::string app, stream, schema;
        if (!ParseStreamInfo(req, app, stream, schema)) {
            res.status = 400;
            res.set_content(R"({"code": -1, "msg": "Invalid request: missing app, stream, or schema"})", 
                          "application/json");
            auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - start_time).count();
            RecordStats("stream_changed", elapsed, false, false, false);
            LOG_WARN("[Hook] stream_changed: 解析流信息失败");
            return;
        }

        // 记录事件
        stats_.stream_changed_count++;
        stats_.total_count++;

        // 处理事件
        bool success = ProcessStreamChanged(app, stream, schema, req.body, false);
        
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - start_time).count();
        
        if (success) {
            res.status = 200;
            res.set_content(R"({"code": 0, "msg": "OK"})", "application/json");
            RecordStats("stream_changed", elapsed, true, false, false);
        } else {
            // 需要重试或外部推流，返回成功（ZLM 要求）
            res.status = 200;
            res.set_content(R"({"code": 0, "msg": "OK"})", "application/json");
            // RecordStats 会在 ProcessStreamChanged 中调用
        }
    } catch (const std::exception& e) {
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - start_time).count();
        RecordStats("stream_changed", elapsed, false, false, false);
        LOG_ERROR("[Hook] stream_changed: 处理异常: {}", e.what());
        res.status = 500;
        res.set_content(R"({"code": -1, "msg": "Internal server error"})", "application/json");
    }
}

void HookHandler::HandleStreamNoneReader(const httplib::Request& req, httplib::Response& res) {
    auto start_time = std::chrono::steady_clock::now();
    
    try {
        std::string app, stream, schema;
        if (!ParseStreamInfo(req, app, stream, schema)) {
            res.status = 400;
            res.set_content(R"({"code": -1, "msg": "Invalid request: missing app, stream, or schema"})", 
                          "application/json");
            auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - start_time).count();
            RecordStats("stream_none_reader", elapsed, false, false, false);
            LOG_WARN("[Hook] stream_none_reader: 解析流信息失败");
            return;
        }

        stats_.stream_none_reader_count++;
        stats_.total_count++;

        LOG_DEBUG("[Hook] stream_none_reader: app={}, stream={}, schema={}", app, stream, schema);

        // 检查流是否存在
        if (!stream_manager_->StreamExists(app, stream)) {
            bool is_external = !IsGatewayManagedStream(app, stream);
            
            if (is_external) {
                // 外部推流，记录日志但不处理
                LOG_DEBUG("[Hook] stream_none_reader: 外部推流，不在 Gateway 管理中: {}/{}", app, stream);
                auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::steady_clock::now() - start_time).count();
                RecordStats("stream_none_reader", elapsed, true, false, true);
            } else {
                // 未注册流，加入重试队列
                LOG_DEBUG("[Hook] stream_none_reader: 流未注册，加入重试队列: {}/{} (可能是时序问题)", app, stream);
                AddRetryTask(RetryTask(app, stream, schema, "stream_none_reader", req.body));
                auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::steady_clock::now() - start_time).count();
                RecordStats("stream_none_reader", elapsed, false, false, false);
            }
            
            res.status = 200;
            res.set_content(R"({"code": 0, "msg": "OK"})", "application/json");
            return;
        }

        // 获取流元数据
        auto metadata = stream_manager_->GetStreamMetadata(app, stream);
        metadata.reader_count = 0;
        stream_manager_->UpdateStreamMetadata(app, stream, metadata);

        LOG_INFO("[Hook] stream_none_reader: 流无播放器: {}/{}", app, stream);

        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - start_time).count();
        RecordStats("stream_none_reader", elapsed, true, false, false);

        res.status = 200;
        // Critical Fix: Explicitly tell ZLM NOT to close the stream (close: false)
        // This prevents the "Auto-Stop" issue where streams die after 60s of inactivity.
        res.set_content(R"({"code": 0, "close": false, "msg": "Keep alive for gateway"})", "application/json");
    } catch (const std::exception& e) {
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - start_time).count();
        RecordStats("stream_none_reader", elapsed, false, false, false);
        LOG_ERROR("[Hook] stream_none_reader: 处理异常: {}", e.what());
        res.status = 500;
        res.set_content(R"({"code": -1, "msg": "Internal server error"})", "application/json");
    }
}

void HookHandler::HandlePlay(const httplib::Request& req, httplib::Response& res) {
    auto start_time = std::chrono::steady_clock::now();
    
    try {
        std::string app, stream, schema;
        if (!ParseStreamInfo(req, app, stream, schema)) {
            res.status = 400;
            res.set_content(R"({"code": -1, "msg": "Invalid request"})", "application/json");
            auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - start_time).count();
            RecordStats("play", elapsed, false, false, false);
            return;
        }

        stats_.play_count++;
        stats_.total_count++;

        LOG_DEBUG("[Hook] play: app={}, stream={}, schema={}", app, stream, schema);

        // 检查流是否存在
        if (!stream_manager_->StreamExists(app, stream)) {
            bool is_external = !IsGatewayManagedStream(app, stream);
            auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - start_time).count();
            RecordStats("play", elapsed, true, false, is_external);
            
            // 允许播放（外部推流或未注册流都可以播放）
            res.status = 200;
            res.set_content(R"({"code": 0, "msg": "OK"})", "application/json");
            return;
        }

        // 获取流元数据并更新 reader_count
        auto metadata = stream_manager_->GetStreamMetadata(app, stream);
        metadata.reader_count++;
        stream_manager_->UpdateStreamMetadata(app, stream, metadata);

        LOG_DEBUG("[Hook] play: 流播放: {}/{}", app, stream);

        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - start_time).count();
        RecordStats("play", elapsed, true, false, false);

        res.status = 200;
        res.set_content(R"({"code": 0, "msg": "OK"})", "application/json");
    } catch (const std::exception& e) {
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - start_time).count();
        RecordStats("play", elapsed, false, false, false);
        LOG_ERROR("[Hook] play: 处理异常: {}", e.what());
        res.status = 500;
        res.set_content(R"({"code": -1, "msg": "Internal server error"})", "application/json");
    }
}

void HookHandler::HandlePublish(const httplib::Request& req, httplib::Response& res) {
    auto start_time = std::chrono::steady_clock::now();
    
    try {
        std::string app, stream, schema;
        if (!ParseStreamInfo(req, app, stream, schema)) {
            res.status = 400;
            res.set_content(R"({"code": -1, "msg": "Invalid request"})", "application/json");
            auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - start_time).count();
            RecordStats("publish", elapsed, false, false, false);
            return;
        }

        stats_.publish_count++;
        stats_.total_count++;

        LOG_DEBUG("[Hook] publish: app={}, stream={}, schema={}", app, stream, schema);

        // 检查流是否存在
        if (!stream_manager_->StreamExists(app, stream)) {
            bool is_external = !IsGatewayManagedStream(app, stream);
            auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - start_time).count();
            RecordStats("publish", elapsed, true, false, is_external);
            
            // 允许推流（外部推流或未注册流都可以推流）
            res.status = 200;
            res.set_content(R"({"code": 0, "msg": "OK"})", "application/json");
            return;
        }

        // 推流成功，通过状态机接口更新状态
        // 使用 OnZLMStreamState 来统一处理 ZLM 上报的状态变化
        stream_manager_->OnZLMStreamState(app, stream, true, true, 0, 0, 0);
            LOG_INFO("[Hook] publish: 流推流成功: {}/{}", app, stream);

        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - start_time).count();
        RecordStats("publish", elapsed, true, false, false);

        res.status = 200;
        res.set_content(R"({"code": 0, "msg": "OK"})", "application/json");
    } catch (const std::exception& e) {
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - start_time).count();
        RecordStats("publish", elapsed, false, false, false);
        LOG_ERROR("[Hook] publish: 处理异常: {}", e.what());
        res.status = 500;
        res.set_content(R"({"code": -1, "msg": "Internal server error"})", "application/json");
    }
}

bool HookHandler::ProcessStreamChanged(const std::string& app, const std::string& stream,
                                      const std::string& schema, const std::string& body, bool is_retry) {
    try {
        json body_json = json::parse(body);
        bool regist = body_json.value("regist", false);
        bool alive = body_json.value("alive", false);
        int reader_count = body_json.value("reader_count", 0);
        int64_t bytes_speed = body_json.value("bytes_speed", 0);
        int64_t total_bytes = body_json.value("total_bytes", 0);
        
        // 重要：检查ZLM的hook通知中是否包含bytes_speed和total_bytes字段
        // 如果字段名不同（如bytesSpeed、totalBytes），尝试使用不同的字段名
        if (bytes_speed == 0 && body_json.contains("bytesSpeed")) {
            if (body_json["bytesSpeed"].is_number()) {
                bytes_speed = body_json["bytesSpeed"].get<int64_t>();
            } else if (body_json["bytesSpeed"].is_string()) {
                try {
                    bytes_speed = std::stoll(body_json["bytesSpeed"].get<std::string>());
                } catch (...) {
                    bytes_speed = 0;
                }
            }
        }
        if (total_bytes == 0 && body_json.contains("totalBytes")) {
            if (body_json["totalBytes"].is_number()) {
                total_bytes = body_json["totalBytes"].get<int64_t>();
            } else if (body_json["totalBytes"].is_string()) {
                try {
                    total_bytes = std::stoll(body_json["totalBytes"].get<std::string>());
                } catch (...) {
                    total_bytes = 0;
                }
            }
        }

        LOG_DEBUG("[Hook] stream_changed{}: app={}, stream={}, schema={}, regist={}, alive={}, readers={}, bytes_speed={}, total_bytes={}", 
                 is_retry ? " (重试)" : "", app, stream, schema, regist, alive, reader_count, bytes_speed, total_bytes);
        
        // 调试：输出hook通知的原始body JSON（仅对GB28181流，避免日志过多）
        if (app == "gb28181" && !is_retry) {
            LOG_DEBUG("[Hook] stream_changed body JSON: {}", body);
        }

        // 检查流是否存在
        std::string matched_stream = stream;
        if (!stream_manager_->StreamExists(app, stream)) {
            // 重要：对于GB28181流，ZLM的hook通知可能使用rtp_stream_id（包含时间戳），
            // 而StreamManager注册时使用target_stream（不包含时间戳）
            // 尝试匹配：如果stream包含时间戳（格式：xxx_timestamp），尝试去掉时间戳后匹配
            // 注意：现在GB28181流的app可能是"live"而不是"gb28181"（因为OpenRtpServer使用target_app）
            if (stream.find('_') != std::string::npos) {
                // 检查是否是GB28181流的rtp_stream_id格式（包含时间戳）
                // 格式：device_id_channel_id_timestamp 或 test_gb28181_xxx_timestamp
                // 尝试去掉最后一个下划线后的时间戳部分
                size_t last_underscore = stream.find_last_of('_');
                if (last_underscore != std::string::npos && last_underscore < stream.length() - 1) {
                    // 检查最后一个下划线后的部分是否是时间戳（纯数字，长度>=10）
                    std::string possible_timestamp = stream.substr(last_underscore + 1);
                    bool is_timestamp = !possible_timestamp.empty() && 
                                       possible_timestamp.length() >= 10 &&
                                       std::all_of(possible_timestamp.begin(), possible_timestamp.end(), ::isdigit);
                    
                    if (is_timestamp) {
                        // 去掉时间戳部分，尝试匹配target_stream
                        std::string target_stream = stream.substr(0, last_underscore);
                        if (stream_manager_->StreamExists(app, target_stream)) {
                            // 验证是否是GB28181流（通过检查gateway_type）
                            auto metadata = stream_manager_->GetStreamMetadata(app, target_stream);
                            if (metadata.gateway_type == "gb28181_gateway") {
                                matched_stream = target_stream;
                                LOG_DEBUG("[Hook] stream_changed: GB28181流名称匹配成功: {} -> {} (去掉时间戳)", 
                                        stream, target_stream);
                            } else {
                                // 如果不是GB28181流，但流名称匹配，也使用（可能是其他类型的流）
                                matched_stream = target_stream;
                                LOG_DEBUG("[Hook] stream_changed: 流名称匹配成功（非GB28181）: {} -> {} (去掉时间戳)", 
                                        stream, target_stream);
                            }
                        }
                    }
                }
            }
            
            // 如果仍然找不到，检查是否是外部推流
            if (!stream_manager_->StreamExists(app, matched_stream)) {
                bool is_external = !IsGatewayManagedStream(app, stream);
                
                if (is_external) {
                    // 外部推流，记录日志但不处理
                    LOG_DEBUG("[Hook] stream_changed: 外部推流，不在 Gateway 管理中: {}/{}", app, stream);
                    return true;  // 返回 true 表示已处理（虽然是外部推流）
                } else {
                    // 未注册流，如果是重试且超过最大重试次数，放弃
                    if (is_retry) {
                        LOG_WARN("[Hook] stream_changed: 重试后流仍不存在，放弃处理: {}/{}", app, stream);
                        return false;
                    }
                    
                    // 加入重试队列
                    LOG_DEBUG("[Hook] stream_changed: 流未注册，加入重试队列: {}/{} (可能是时序问题)", app, stream);
                    AddRetryTask(RetryTask(app, stream, schema, "stream_changed", body));
                    return false;  // 返回 false 表示需要重试
                }
            }
        }

        // 通过 StreamManager 的统一入口更新 ZLM 状态和本地状态机
        // 使用matched_stream（可能是去掉时间戳后的target_stream）
        stream_manager_->OnZLMStreamState(app, matched_stream, regist, alive,
                                          reader_count, bytes_speed, total_bytes);

        LOG_INFO("[Hook] stream_changed{}: 更新流状态成功: {}/{} (regist={}, alive={}){}", 
                is_retry ? " (重试)" : "", app, matched_stream, regist, alive,
                (matched_stream != stream) ? " (流名称已匹配: " + stream + " -> " + matched_stream + ")" : "");

        return true;
    } catch (const std::exception& e) {
        LOG_ERROR("[Hook] stream_changed: 处理异常: {}", e.what());
        return false;
    }
}

bool HookHandler::IsGatewayManagedStream(const std::string& /* app */, const std::string& stream) const {
    // 判断流是否应该是 Gateway 管理的
    // Gateway 创建的流通常有以下特征：
    // 1. 流名包含协议转换信息（如 test_rtsp_http-flv_xxx）
    // 2. 流名包含时间戳（Gateway 自动生成的流名）
    // 3. 流名包含特定前缀（如 test_rtsp_, test_httpflv_ 等）
    
    // 检查流名是否包含 Gateway 创建的流名模式
    // 常见的 Gateway 流名模式：
    // - test_rtsp_http-flv_<timestamp>
    // - test_rtsp_hls_<timestamp>
    // - test_httpflv_<timestamp>
    // - test_rtmp_<timestamp>
    
    if (stream.find("_rtsp_") != std::string::npos ||
        stream.find("_httpflv_") != std::string::npos ||
        stream.find("_rtmp_") != std::string::npos ||
        stream.find("_hls_") != std::string::npos ||
        stream.find("_dash_") != std::string::npos ||
        stream.find("_quic_") != std::string::npos) {
        // 包含 Gateway 流名模式，应该是 Gateway 管理的流
        return true;
    }
    
    // 检查是否是简单的测试源流（如 test_source），这些通常是外部推流
    if (stream == "test_source" || stream.find("test_source") == 0) {
        return false;
    }
    
    // 默认认为可能是 Gateway 管理的流（保守策略）
    // 这样可以确保时序问题导致的未注册流会被重试
    return true;
}

void HookHandler::AddRetryTask(const RetryTask& task) {
    std::lock_guard<std::mutex> lock(retry_queue_mutex_);
    retry_queue_.push(task);
    stats_.retry_count++;
    LOG_DEBUG("[Hook] 添加重试任务: {}/{} (重试次数: {})", task.app, task.stream, task.retry_count);
}

void HookHandler::RetryThreadLoop() {
    LOG_INFO("[Hook] 重试线程启动");
    
    while (retry_running_) {
        std::this_thread::sleep_for(std::chrono::milliseconds(config::constants::time::HOOK_CHECK_INTERVAL_MS));
        
        std::queue<RetryTask> tasks_to_retry;
        
        // 从队列中取出需要重试的任务
        {
            std::lock_guard<std::mutex> lock(retry_queue_mutex_);
            auto now = std::chrono::steady_clock::now();
            
            while (!retry_queue_.empty()) {
                auto task = retry_queue_.front();
                retry_queue_.pop();
                
                if (now >= task.retry_time) {
                    // 到重试时间了
                    if (task.retry_count < MAX_RETRY_COUNT) {
                        tasks_to_retry.push(task);
                    } else {
                        // 超过最大重试次数，放弃
                        LOG_WARN("[Hook] 重试任务超过最大重试次数，放弃: {}/{} (已重试 {} 次)", 
                                task.app, task.stream, task.retry_count);
                        stats_.failed_count++;
                    }
                } else {
                    // 还没到重试时间，放回队列
                    retry_queue_.push(task);
                    break;  // 队列是按时间排序的，后面的任务时间更晚
                }
            }
        }
        
        // 处理需要重试的任务
        while (!tasks_to_retry.empty()) {
            auto task = tasks_to_retry.front();
            tasks_to_retry.pop();
            
            LOG_DEBUG("[Hook] 重试处理: {}/{} (第 {} 次重试)", 
                     task.app, task.stream, task.retry_count + 1);
            
            auto start_time = std::chrono::steady_clock::now();
            bool success = false;
            
            if (task.event_type == "stream_changed") {
                success = ProcessStreamChanged(task.app, task.stream, task.schema, 
                                              task.request_body, true);
            } else if (task.event_type == "stream_none_reader") {
                // stream_none_reader 的重试逻辑
                if (stream_manager_->StreamExists(task.app, task.stream)) {
                    auto metadata = stream_manager_->GetStreamMetadata(task.app, task.stream);
                    metadata.reader_count = 0;
                    stream_manager_->UpdateStreamMetadata(task.app, task.stream, metadata);
                    success = true;
                    LOG_INFO("[Hook] stream_none_reader (重试): 更新成功: {}/{}", 
                            task.app, task.stream);
                } else {
                    // 流仍不存在，创建新的重试任务
                    if (task.retry_count < MAX_RETRY_COUNT) {
                        RetryTask new_task(task.app, task.stream, task.schema, 
                                          task.event_type, task.request_body, 
                                          task.retry_count + 1);
                        AddRetryTask(new_task);
                    }
                    success = false;
                }
            }
            
            auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - start_time).count();
            
            if (success) {
                RecordStats(task.event_type, elapsed, true, true, false);
                LOG_INFO("[Hook] 重试成功: {}/{}", task.app, task.stream);
            } else {
                // 重试失败，如果还没超过最大重试次数，创建新的重试任务
                if (task.retry_count < MAX_RETRY_COUNT) {
                    RetryTask new_task(task.app, task.stream, task.schema, 
                                      task.event_type, task.request_body, 
                                      task.retry_count + 1);
                    AddRetryTask(new_task);
                } else {
                    RecordStats(task.event_type, elapsed, false, true, false);
                }
            }
        }
    }
    
    LOG_INFO("[Hook] 重试线程退出");
}

void HookHandler::RecordStats(const std::string& event_type, int64_t processing_time_ms,
                              bool success, bool is_retry, bool is_external) {
    stats_.total_processing_time_ms += processing_time_ms;
    
    if (success) {
        stats_.success_count++;
    } else {
        stats_.failed_count++;
    }
    
    if (is_external) {
        stats_.external_stream_count++;
    }
    
    // 记录详细的统计信息（可选，用于调试）
    if (processing_time_ms > 100) {
        LOG_DEBUG("[Hook] 性能: {} 处理时间 {}ms (成功: {}, 重试: {}, 外部: {})", 
                 event_type, processing_time_ms, success, is_retry, is_external);
    }
}

HookEventStats HookHandler::GetStats() const {
    HookEventStats result;
    result.total_count = stats_.total_count.load();
    result.success_count = stats_.success_count.load();
    result.retry_count = stats_.retry_count.load();
    result.failed_count = stats_.failed_count.load();
    result.external_stream_count = stats_.external_stream_count.load();
    result.total_processing_time_ms = stats_.total_processing_time_ms.load();
    result.stream_changed_count = stats_.stream_changed_count.load();
    result.stream_none_reader_count = stats_.stream_none_reader_count.load();
    result.play_count = stats_.play_count.load();
    result.publish_count = stats_.publish_count.load();
    return result;
}

bool HookHandler::ParseStreamInfo(const httplib::Request& req, 
                                 std::string& app, 
                                 std::string& stream, 
                                 std::string& schema) {
    try {
        json body = json::parse(req.body);
        
        app = body.value("app", "");
        stream = body.value("stream", "");
        schema = body.value("schema", "");

        if (app.empty() || stream.empty() || schema.empty()) {
            LOG_WARN("[Hook] 解析流信息失败: app={}, stream={}, schema={}", app, stream, schema);
            return false;
        }

        return true;
    } catch (const json::exception& e) {
        LOG_WARN("[Hook] JSON 解析失败: {}", e.what());
        return false;
    }
}

std::string HookHandler::SchemaToOutputProtocol(const std::string& schema) {
    // 使用静态 map 映射 ZLM schema 到 Gateway output_protocol
    using namespace gateway::Protocol;
    static const std::unordered_map<std::string, std::string> schema_map = {
        {"http-flv", HTTP_FLV},
        {"flv", HTTP_FLV},
        {"hls", HLS},
        {"rtmp", RTMP},
        {"rtsp", RTSP},
        {"webrtc", WEBRTC}
    };
    
    auto it = schema_map.find(schema);
    return (it != schema_map.end()) ? it->second : "";
}

void HookHandler::HandleStreamNotFound(const httplib::Request& req, httplib::Response& res) {
    auto start_time = std::chrono::steady_clock::now();
    
    try {
        std::string app, stream, schema;
        if (!ParseStreamInfo(req, app, stream, schema)) {
            // 解析失败，拒绝拉流
            res.status = 200;
            res.set_content(R"({"code": 0, "close": true})", "application/json");
            auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - start_time).count();
            RecordStats("stream_not_found", elapsed, false, false, false);
            LOG_WARN("[Hook] stream_not_found: 解析流信息失败，拒绝拉流");
            return;
        }

        LOG_INFO("[Hook] stream_not_found: app={}, stream={}, schema={}", app, stream, schema);

        // 检查是否允许按需拉流
        // 目前只对 live/ 下的流启用按需拉流
        if (app != "live") {
            res.status = 200;
            res.set_content(R"({"code": 0, "close": true})", "application/json");
            LOG_DEBUG("[Hook] stream_not_found: app={} 不在允许列表中，拒绝拉流", app);
            return;
        }

        // 检查流是否已经在 StreamManager 中（避免重复创建）
        if (stream_manager_->StreamExists(app, stream)) {
            auto metadata = stream_manager_->GetStreamMetadata(app, stream);
            // 如果流已经存在且不是 Stopped 状态，说明可能正在创建中，允许等待
            if (metadata.status != streaming::StreamStatus::Stopped) {
                res.status = 200;
                res.set_content(R"({"code": 0, "close": false})", "application/json");
                LOG_DEBUG("[Hook] stream_not_found: 流已存在且状态为 {}，允许等待", 
                         static_cast<int>(metadata.status));
                return;
            }
        }

        // 实现按需拉流逻辑
        if (!stream_handler_) {
            res.status = 200;
            res.set_content(R"({"code": 0, "close": true})", "application/json");
            LOG_WARN("[Hook] stream_not_found: StreamHandler 未初始化，无法按需拉流: {}/{}", app, stream);
            auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - start_time).count();
            RecordStats("stream_not_found", elapsed, false, false, false);
            return;
        }

        // 从流名推断 source_url 和 protocol
        std::string source_url, protocol;
        bool inferred = false;

        // 优先使用 StreamManager 中已存在的配置
        if (stream_manager_->StreamExists(app, stream)) {
            auto metadata = stream_manager_->GetStreamMetadata(app, stream);
            if (!metadata.source_url.empty()) {
                source_url = metadata.source_url;
                protocol = metadata.protocol; // 使用配置的协议（如 rtmp）
                inferred = true;
                LOG_INFO("[Hook] stream_not_found: 使用现有流配置: {}/{} -> source={}, protocol={}", 
                         app, stream, source_url, protocol);
            }
        }

        // 如果没有现有配置，尝试从流名推断
        if (!inferred) {
            if (InferSourceUrl(app, stream, schema, source_url, protocol)) {
                inferred = true;
            }
        }

        if (!inferred) {
            res.status = 200;
            res.set_content(R"({"code": 0, "close": true})", "application/json");
            LOG_INFO("[Hook] stream_not_found: 无法推断 source_url 且无现有配置，拒绝拉流: {}/{}", app, stream);
            auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - start_time).count();
            RecordStats("stream_not_found", elapsed, false, false, false);
            return;
        }

        LOG_INFO("[Hook] stream_not_found: 推断 source_url={}, protocol={}, 开始按需拉流: {}/{}", 
                 source_url, protocol, app, stream);

        // 将 schema 转换为 output_protocol
        std::string output_protocol = SchemaToOutputProtocol(schema);
        if (output_protocol.empty()) {
            output_protocol = "http-flv";  // 默认使用 http-flv
        }

        // 创建流（通过 StreamHandler）
        // 注意：这里需要异步处理，因为创建流可能需要时间
        // 但 ZLM 的 Hook 要求快速响应，所以我们先返回允许，然后在后台创建流
        res.status = 200;
        res.set_content(R"({"code": 0, "close": false})", "application/json");
        
        // 在后台线程中创建流
        std::thread([this, app, stream, source_url, protocol, output_protocol]() {
            try {
                LOG_INFO("[Hook] stream_not_found: 后台创建流开始: {}/{} (protocol: {}, source: {})", 
                         app, stream, protocol, source_url);
                
                if (stream_handler_) {
                    bool success = stream_handler_->CreateStreamOnDemand(
                        app, stream, source_url, protocol, output_protocol);
                    if (success) {
                        LOG_INFO("[Hook] stream_not_found: 按需拉流成功: {}/{}", app, stream);
                    } else {
                        LOG_WARN("[Hook] stream_not_found: 按需拉流失败: {}/{}", app, stream);
                    }
                } else {
                    LOG_ERROR("[Hook] stream_not_found: StreamHandler 未初始化，无法创建流");
                }
                
            } catch (const std::exception& e) {
                LOG_ERROR("[Hook] stream_not_found: 后台创建流失败: {}", e.what());
            }
        }).detach();
        
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - start_time).count();
        RecordStats("stream_not_found", elapsed, true, false, false);
        
    } catch (const std::exception& e) {
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - start_time).count();
        RecordStats("stream_not_found", elapsed, false, false, false);
        LOG_ERROR("[Hook] stream_not_found: 处理异常: {}", e.what());
        res.status = 200;
        res.set_content(R"({"code": 0, "close": true})", "application/json");
    }
}

bool HookHandler::InferSourceUrl(const std::string& /* app */, const std::string& stream, 
                                 const std::string& /* schema */,
                                 std::string& source_url, std::string& protocol) {
    // 策略1: 从流名中解析（格式：{protocol}_{ip}_{port}_{path}）
    // 例如: rtsp_192.168.1.100_554_stream1 -> rtsp://192.168.1.100:554/stream1
    
    // 检查流名是否包含下划线分隔的协议信息
    size_t first_underscore = stream.find('_');
    if (first_underscore != std::string::npos && first_underscore < stream.length() - 1) {
        std::string prefix = stream.substr(0, first_underscore);
        
        // 检查是否是已知的协议前缀
        if (prefix == "rtsp" || prefix == "rtmp" || prefix == "http" || prefix == "hls") {
            std::string remaining = stream.substr(first_underscore + 1);
            
            // 尝试解析 IP:PORT/PATH 格式
            size_t second_underscore = remaining.find('_');
            if (second_underscore != std::string::npos) {
                std::string ip = remaining.substr(0, second_underscore);
                std::string port_and_path = remaining.substr(second_underscore + 1);
                
                size_t third_underscore = port_and_path.find('_');
                if (third_underscore != std::string::npos) {
                    std::string port = port_and_path.substr(0, third_underscore);
                    std::string path = port_and_path.substr(third_underscore + 1);
                    
                    // 构建 source_url
                    if (prefix == "rtsp") {
                        source_url = "rtsp://" + ip + ":" + port + "/" + path;
                        protocol = "rtsp";
                        return true;
                    } else if (prefix == "rtmp") {
                        source_url = "rtmp://" + ip + ":" + port + "/" + path;
                        protocol = "rtmp";
                        return true;
                    } else if (prefix == "http") {
                        source_url = "http://" + ip + ":" + port + "/" + path;
                        protocol = "http-flv";
                        return true;
                    } else if (prefix == "hls") {
                        source_url = "http://" + ip + ":" + port + "/" + path;
                        protocol = "hls";
                        return true;
                    }
                }
            }
        }
    }
    
    // 策略2: 根据 schema 和流名使用默认规则
    // 例如：对于 RTSP，尝试 rtsp://192.168.1.1:554/{stream}
    // 注意：这是一个简单的示例，实际使用时应该从配置中读取
    
    // 暂时返回 false，表示无法推断
    // 后续可以扩展为从配置文件或数据库中查找映射规则
    return false;
}

} // namespace handlers
} // namespace api
