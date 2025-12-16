#include "streaming/zlmediakit/zlm_client.hpp"
#include "utils/logger.hpp"
#include <sstream>
#include <iostream>
#include <limits>
#include <algorithm>
#include <cctype>
#include <fstream>
#include <chrono>

using json = nlohmann::json;

namespace streaming {

// CURL 写回调函数
static size_t WriteCallback(void* contents, size_t size, size_t nmemb, std::string* data) {
    size_t total_size = size * nmemb;
    data->append((char*)contents, total_size);
    return total_size;
}

ZLMClient::ZLMClient(const std::string& api_url, const std::string& secret)
    : api_url_(api_url), secret_(secret) {
    // CURL 实例现在在每次请求时临时创建，无需在构造函数中初始化
}

ZLMClient::~ZLMClient() {
    // CURL 实例现在在每次请求完成后立即清理，无需在析构函数中清理
}


bool ZLMClient::AddRTMPStream(const std::string& app,
                              const std::string& stream,
                              const std::string& url) {
    json params = {
        {"secret", secret_},
        {"vhost", "__defaultVhost__"},
        {"app", app},
        {"stream", stream},
        {"url", url},
        {"schema", ""},
        {"retry_count", 3},
        {"timeout_sec", 20}  // RTMP 连接超时时间（秒），20秒适合大多数场景
    };

    auto response = Request("/index/api/addStreamProxy", params);
    bool success = false;
    if (response.contains("code")) {
        if (response["code"].is_number()) {
            success = response["code"].get<int>() == 0;
        } else if (response["code"].is_string()) {
            success = response["code"].get<std::string>() == "0";
        }
    }
    
    if (success) {
        LOG_INFO("添加 RTMP 推流成功: {}/{} -> {}", app, stream, url);
        return true;
    } else {
        std::string msg;
        if (response.contains("msg")) {
            msg = response["msg"].get<std::string>();
        }
        
        // 如果流已存在，视为成功（幂等性）
        if (msg.find("already exists") != std::string::npos) {
            LOG_WARN("添加 RTMP 推流: 流已存在 (视为成功): {}/{} -> {}", app, stream, url);
            return true;
        }

        LOG_ERROR("添加 RTMP 推流失败: {}/{} -> {}", app, stream, url);
        if (!msg.empty()) {
            LOG_ERROR("错误信息: {}", msg);
        }
        return false;
    }
}

bool ZLMClient::AddRTSPStream(const std::string& app,
                              const std::string& stream,
                              const std::string& url) {
    json params = {
        {"secret", secret_},
        {"vhost", "__defaultVhost__"},
        {"app", app},
        {"stream", stream},
        {"url", url},
        {"schema", ""},
        {"retry_count", 3},
        {"timeout_sec", 30}  // RTSP 连接超时时间（秒），30秒适合 RTSP 连接建立可能需要更长时间的场景
    };

    auto response = Request("/index/api/addStreamProxy", params);
    bool success = false;
    if (response.contains("code")) {
        if (response["code"].is_number()) {
            success = response["code"].get<int>() == 0;
        } else if (response["code"].is_string()) {
            success = response["code"].get<std::string>() == "0";
        }
    }
    
    if (success) {
        LOG_INFO("添加 RTSP 推流成功: {}/{} -> {}", app, stream, url);
        return true;
    } else {
        std::string msg;
        if (response.contains("msg")) {
            msg = response["msg"].get<std::string>();
        }
        
        // 如果流已存在，视为成功（幂等性）
        if (msg.find("already exists") != std::string::npos) {
            LOG_WARN("添加 RTSP 推流: 流已存在 (视为成功): {}/{} -> {}", app, stream, url);
            return true;
        }

        LOG_ERROR("添加 RTSP 推流失败: {}/{} -> {}", app, stream, url);
        if (!msg.empty()) {
            LOG_ERROR("错误信息: {}", msg);
        }
        return false;
    }
}

bool ZLMClient::AddStreamProxy(const std::string& app,
                               const std::string& stream,
                               const std::string& url) {
    // 根据 URL 协议类型设置不同的超时时间
    // HTTP-FLV 和 HLS 是长连接流，需要更长的超时时间
    int timeout_sec = 30;  // 默认超时时间
    std::string lower_url = url;
    std::transform(lower_url.begin(), lower_url.end(), lower_url.begin(), ::tolower);
    if (lower_url.find("http://") == 0 || lower_url.find("https://") == 0) {
        // HTTP/HTTPS 协议（包括 HTTP-FLV、HLS），使用更长的超时时间
        // 因为长连接流不会立即返回数据，需要等待
        timeout_sec = 60;  // 增加到 60 秒，给长连接流更多时间
    } else if (lower_url.find("rtsp://") == 0) {
        // RTSP 协议，使用中等超时时间
        timeout_sec = 30;
    } else if (lower_url.find("rtmp://") == 0) {
        // RTMP 协议，使用较短超时时间
        timeout_sec = 20;
    }
    
    json params = {
        {"secret", secret_},
        {"vhost", "__defaultVhost__"},
        {"app", app},
        {"stream", stream},
        {"url", url},
        {"schema", ""},
        {"retry_count", 3},
        {"timeout_sec", timeout_sec}
    };

    auto response = Request("/index/api/addStreamProxy", params);
    bool success = false;
    if (response.contains("code")) {
        if (response["code"].is_number()) {
            success = response["code"].get<int>() == 0;
        } else if (response["code"].is_string()) {
            success = response["code"].get<std::string>() == "0";
        }
    }
    
    if (success) {
        LOG_INFO("添加流代理成功: {}/{} -> {}", app, stream, url);
        return true;
    } else {
        std::string msg;
        if (response.contains("msg")) {
            msg = response["msg"].get<std::string>();
        }
        
        // 如果流已存在，视为成功（幂等性）
        if (msg.find("already exists") != std::string::npos) {
            LOG_WARN("添加流代理: 流已存在 (视为成功): {}/{} -> {}", app, stream, url);
            return true;
        }

        // 对于 HTTP-FLV 等长连接流，即使 API 返回超时，流可能已经在 ZLM 中创建
        // 检查流是否真的存在
        if (msg.find("timeout") != std::string::npos || msg.find("wait http response body timeout") != std::string::npos) {
            std::string lower_url_check = url;
            std::transform(lower_url_check.begin(), lower_url_check.end(), lower_url_check.begin(), ::tolower);
            if (lower_url_check.find("http://") == 0 || lower_url_check.find("https://") == 0) {
                // HTTP/HTTPS 协议，即使 API 超时，流可能已经创建
                // 返回 true，让上层继续检查流是否真的存在
                LOG_WARN("添加流代理 API 返回超时，但流可能已创建，继续检查: {}/{} -> {}", app, stream, url);
                return true;  // 返回 true，让上层继续检查
            }
        }

        LOG_ERROR("添加流代理失败: {}/{} -> {}", app, stream, url);
        if (!msg.empty()) {
            LOG_ERROR("错误信息: {}", msg);
        } else {
            LOG_ERROR("错误信息: (空) - API 响应: {}", response.dump());
        }
        return false;
    }
}

bool ZLMClient::DeleteStream(const std::string& app, const std::string& stream) {
    // ZLMediaKit 删除流有两种方式：
    // 1. delStreamProxy: 删除代理流（通过 addStreamProxy 添加的流）
    // 2. close_streams: 关闭所有流（包括直接推送的流）
    
    // 先尝试 delStreamProxy（适用于代理流）
    std::string key = "__defaultVhost__/" + app + "/" + stream;
    
    json params = {
        {"secret", secret_},
        {"key", key}
    };

    auto response = Request("/index/api/delStreamProxy", params);
    bool success = false;
    if (response.contains("code")) {
        if (response["code"].is_number()) {
            success = response["code"].get<int>() == 0;
        } else if (response["code"].is_string()) {
            success = response["code"].get<std::string>() == "0";
        }
    }
    
    if (success) {
        LOG_INFO("删除流成功（delStreamProxy）: {}/{} (key: {})", app, stream, key);
        return true;
    }
    
    // 如果 delStreamProxy 失败，尝试 close_streams（适用于直接推送的流）
    LOG_DEBUG("delStreamProxy 失败，尝试 close_streams: {}/{}", app, stream);
    json close_params = {
        {"secret", secret_},
        {"schema", ""},  // 空字符串表示所有协议
        {"vhost", "__defaultVhost__"},
        {"app", app},
        {"stream", stream}
    };
    
    auto close_response = Request("/index/api/close_streams", close_params);
    bool close_success = false;
    if (close_response.contains("code")) {
        if (close_response["code"].is_number()) {
            close_success = close_response["code"].get<int>() == 0;
        } else if (close_response["code"].is_string()) {
            close_success = close_response["code"].get<std::string>() == "0";
        }
    }
    
    if (close_success) {
        LOG_INFO("删除流成功（close_streams）: {}/{}", app, stream);
        return true;
    } else {
        LOG_ERROR("删除流失败: {}/{} (delStreamProxy 和 close_streams 都失败)", app, stream);
        if (close_response.contains("msg")) {
            LOG_ERROR("close_streams 错误信息: {}", close_response["msg"].get<std::string>());
        }
        return false;
    }
}

std::vector<StreamInfo> ZLMClient::GetStreamList(const std::string& schema) {
    json params = {
        {"secret", secret_},
        {"schema", schema},
        {"vhost", "__defaultVhost__"},
        {"app", ""},
        {"stream", ""}
    };

    auto response = Request("/index/api/getMediaList", params);
    std::vector<StreamInfo> streams;
    
    if (!response.contains("code")) {
        return streams;
    }

    if (response.contains("data") && response["data"].is_array()) {
        for (const auto& item : response["data"]) {
            try {
                streams.push_back(ParseStreamInfo(item));
            } catch (const std::exception& e) {
                LOG_ERROR("解析流信息失败: {}", e.what());
            }
        }
    }

    return streams;
}

StreamInfo ZLMClient::GetStreamInfo(const std::string& app, const std::string& stream, const std::string& schema) {
    json params = {
        {"secret", secret_},
        {"schema", schema},
        {"vhost", "__defaultVhost__"},
        {"app", app},
        {"stream", stream}
    };

    auto response = Request("/index/api/getMediaList", params);
    if (response.contains("data") && response["data"].is_array() && 
        !response["data"].empty()) {
        try {
            return ParseStreamInfo(response["data"][0]);
        } catch (const std::exception& e) {
            LOG_ERROR("解析流信息失败: {}", e.what());
            return StreamInfo{};
        }
    }

    return StreamInfo{};
}

bool ZLMClient::IsStreamActive(const StreamInfo& stream_info) {
    // 流必须存在
    if (stream_info.app.empty()) {
        return false;
    }
    
    // 检查流是否活跃：alive标志、数据传输或累计数据
    return stream_info.alive || stream_info.bytes_speed > 0 || stream_info.total_bytes > 0;
}

bool ZLMClient::IsOnline() {
    json params = {
        {"secret", secret_}
    };

    auto response = Request("/index/api/getServerConfig", params);
    if (!response.contains("code")) {
        return false;
    }
    
    // 安全比较 code 字段（可能是数字或字符串）
    try {
        if (response["code"].is_number()) {
            return response["code"].get<int>() == 0;
        } else if (response["code"].is_string()) {
            return response["code"].get<std::string>() == "0";
        }
    } catch (...) {
        // 忽略转换错误
    }
    return false;
}

json ZLMClient::Request(const std::string& api_path, const json& params) {
    // 每次请求创建新的 CURL 实例，避免多线程竞争共享资源导致死锁
    CURL* curl = curl_easy_init();
    if (!curl) {
        LOG_ERROR("创建 CURL 实例失败");
        return json{{"code", -1}, {"msg", "Failed to create CURL instance"}};
    }
    
    std::string response_data;
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response_data);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 3L);  // 总超时 3 秒
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 2L);  // 连接超时 2 秒

    // 构建 URL
    std::string url = api_url_ + api_path;

    // 构建查询参数（URL 编码）
    std::string query_string;
    for (auto it = params.begin(); it != params.end(); ++it) {
        if (!query_string.empty()) {
            query_string += "&";
        }
        query_string += it.key() + "=";
        if (it.value().is_string()) {
            std::string value = it.value().get<std::string>();
            // URL 编码
            char* encoded = curl_easy_escape(curl, value.c_str(), value.length());
            if (encoded) {
                query_string += encoded;
                curl_free(encoded);
            } else {
                query_string += value;
            }
        } else {
            query_string += it.value().dump();
        }
    }

    if (!query_string.empty()) {
        url += "?" + query_string;
    }

    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());

    // 执行请求
    CURLcode res = curl_easy_perform(curl);
    
    // 清理 CURL 实例
    curl_easy_cleanup(curl);
    
    if (res != CURLE_OK) {
        LOG_ERROR("CURL 请求失败: {}", curl_easy_strerror(res));
        return json{{"code", -1}, {"msg", curl_easy_strerror(res)}};
    }

    // 解析响应
    try {
        return json::parse(response_data);
    } catch (const json::exception& e) {
        LOG_ERROR("解析 JSON 响应失败: {}", e.what());
        return json{{"code", -1}, {"msg", "JSON parse error"}};
    }
}

StreamInfo ZLMClient::ParseStreamInfo(const nlohmann::json& j) {
    StreamInfo info;
    
    try {
        // 字符串字段
        // #region agent log
        std::ofstream log_file("/Users/rory/work/gateway/ZLM-gateway/.cursor/debug.log", std::ios::app);
        if (log_file.is_open()) {
            log_file << "{\"timestamp\":" << std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count() << ",\"location\":\"zlm_client.cpp:413\",\"message\":\"ParseStreamInfo entry\",\"sessionId\":\"debug-session\",\"runId\":\"run1\",\"hypothesisId\":\"B\"}\n";
            log_file.close();
        }
        // #endregion
        if (j.contains("app") && j["app"].is_string()) {
            info.app = j["app"].get<std::string>();
            // #region agent log
            log_file.open("/Users/rory/work/gateway/ZLM-gateway/.cursor/debug.log", std::ios::app);
            if (log_file.is_open()) {
                log_file << "{\"timestamp\":" << std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count() << ",\"location\":\"zlm_client.cpp:418\",\"message\":\"Parsed app from ZLM\",\"data\":{\"app\":\"" << info.app.substr(0, 100) << "\",\"app_length\":" << info.app.length() << "},\"sessionId\":\"debug-session\",\"runId\":\"run1\",\"hypothesisId\":\"B\"}\n";
                log_file.close();
            }
            // #endregion
        }
        if (j.contains("stream") && j["stream"].is_string()) {
            info.stream = j["stream"].get<std::string>();
            // #region agent log
            log_file.open("/Users/rory/work/gateway/ZLM-gateway/.cursor/debug.log", std::ios::app);
            if (log_file.is_open()) {
                log_file << "{\"timestamp\":" << std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count() << ",\"location\":\"zlm_client.cpp:419\",\"message\":\"Parsed stream from ZLM\",\"data\":{\"stream\":\"" << info.stream.substr(0, 100) << "\",\"stream_length\":" << info.stream.length() << "},\"sessionId\":\"debug-session\",\"runId\":\"run1\",\"hypothesisId\":\"B\"}\n";
                log_file.close();
            }
            // #endregion
        }
        if (j.contains("schema") && j["schema"].is_string()) info.schema = j["schema"].get<std::string>();
        if (j.contains("vhost") && j["vhost"].is_string()) info.vhost = j["vhost"].get<std::string>();
        if (j.contains("ip") && j["ip"].is_string()) info.ip = j["ip"].get<std::string>();
        
        // 数字字段 - 安全转换（处理可能是字符串或数字的情况）
        auto safeGetInt = [](const nlohmann::json& j, const std::string& key, int& target) {
            if (!j.contains(key)) {
                return;
            }
            try {
                const auto& val = j[key];
                if (val.is_number()) {
                    if (val.is_number_unsigned()) {
                        uint64_t uval = val.get<uint64_t>();
                        target = (uval > static_cast<uint64_t>(std::numeric_limits<int>::max())) 
                                ? std::numeric_limits<int>::max() 
                                : static_cast<int>(uval);
                    } else if (val.is_number_float()) {
                        target = static_cast<int>(val.get<double>());
                    } else {
                        int64_t ival = val.get<int64_t>();
                        if (ival > std::numeric_limits<int>::max()) {
                            target = std::numeric_limits<int>::max();
                        } else if (ival < std::numeric_limits<int>::min()) {
                            target = std::numeric_limits<int>::min();
                        } else {
                            target = static_cast<int>(ival);
                        }
                    }
                } else if (val.is_string()) {
                    target = std::stoi(val.get<std::string>());
                }
            } catch (...) {
                // 转换失败，保持默认值
            }
        };
        
        safeGetInt(j, "port", info.port);
        safeGetInt(j, "readerCount", info.reader_count);
        safeGetInt(j, "totalReaderCount", info.total_reader_count);
        safeGetInt(j, "originType", info.origin_type);
        // originTypeStr 是字符串，不解析为 int（结构体中定义为 int 但实际是字符串，不使用）
        safeGetInt(j, "createStamp", info.create_stamp);
        safeGetInt(j, "aliveSecond", info.alive_second);
        safeGetInt(j, "bytesSpeed", info.bytes_speed);
        // bytesSpeedStr 是字符串，不解析为 int（结构体中定义为 int 但实际是字符串，不使用）
        safeGetInt(j, "totalBytes", info.total_bytes);
        // totalBytesStr 是字符串，不解析为 int（结构体中定义为 int 但实际是字符串，不使用）
        
        if (j.contains("originUrl") && j["originUrl"].is_string()) {
            info.origin_url = j["originUrl"].get<std::string>();
        }
        
        // 注意：origin_type_str, bytes_speed_str, total_bytes_str 在结构体中定义为 int
        // 但 ZLMediaKit 返回的是字符串，这些字段不应该被使用
        
        // 布尔字段 - alive
        // 注意：ZLMediaKit 的 getMediaList API 可能不返回 alive 字段，或者返回的 alive 字段不准确
        // 改进：即使 alive 字段为 false，如果有数据传输，也应该认为流是活跃的
        // 这是因为 ZLMediaKit 的 alive 字段可能有延迟，或者在某些情况下不准确
        if (j.contains("alive")) {
            if (j["alive"].is_boolean()) {
                info.alive = j["alive"].get<bool>();
            } else if (j["alive"].is_number()) {
                info.alive = j["alive"].get<int>() != 0;
            }
        } else {
            // alive 字段不存在，根据其他指标推断
            // 如果流有存活时间、有数据传输或累计数据，则认为流是活跃的
            info.alive = (info.alive_second > 0) || (info.bytes_speed > 0) || (info.total_bytes > 0);
        }
        
        // 改进：即使 ZLMediaKit 返回 alive=false，如果有数据传输，也应该认为流是活跃的
        // 这是因为 ZLMediaKit 的 alive 字段可能有延迟，或者在某些情况下不准确
        // 例如：流代理（stream proxy）可能在创建后立即有数据传输，但 alive 字段可能还是 false
        if (!info.alive && (info.bytes_speed > 0 || info.total_bytes > 0)) {
            info.alive = true;
        }
    } catch (const std::exception& e) {
        LOG_ERROR("ParseStreamInfo 异常: {}", e.what());
        throw;
    }
    
    return info;
}

// ========== GB28181 RTP 服务器 API 实现 ==========

RtpServerInfo ZLMClient::OpenRtpServer(
    const std::string& stream_id,
    uint16_t port,
    int tcp_mode,
    bool enable_rtcp,
    const std::string& local_ip,
    const std::string& ssrc,
    const std::string& app) {
    
    json params = {
        {"secret", secret_},
        {"stream_id", stream_id},
        {"port", port},
        {"tcp_mode", tcp_mode},
        {"enable_rtcp", enable_rtcp ? 1 : 0},
        {"app", app.empty() ? "gb28181" : app},  // 使用传入的app参数，默认为gb28181
        {"vhost", "__defaultVhost__"}  // 明确指定vhost参数
    };
    
    if (!local_ip.empty()) {
        params["local_ip"] = local_ip;
    }
    if (!ssrc.empty()) {
        params["ssrc"] = ssrc;
    }
    
    auto response = PostRequest("/index/api/openRtpServer", params);
    RtpServerInfo info;
    
    bool success = false;
    if (response.contains("code")) {
        if (response["code"].is_number()) {
            success = response["code"].get<int>() == 0;
        } else if (response["code"].is_string()) {
            success = response["code"].get<std::string>() == "0";
        }
    }
    
    if (success) {
        info.stream_id = stream_id;
        
        // ZLM API返回格式可能是 {"code": 0, "port": 32278} 或 {"code": 0, "data": {"port": 32278}}
        // 需要同时支持两种格式
        const json* data_ptr = nullptr;
        if (response.contains("data") && response["data"].is_object()) {
            data_ptr = &response["data"];
        } else {
            // 如果没有data字段，直接从response根级别读取
            data_ptr = &response;
        }
        
        const auto& data = *data_ptr;
        
        if (data.contains("port") && data["port"].is_number()) {
            info.port = data["port"].get<int>();
        }
        if (data.contains("tcp_mode") && data["tcp_mode"].is_number()) {
            info.tcp_mode = data["tcp_mode"].get<int>();
        }
        if (data.contains("enable_rtcp")) {
            if (data["enable_rtcp"].is_boolean()) {
                info.enable_rtcp = data["enable_rtcp"].get<bool>();
            } else if (data["enable_rtcp"].is_number()) {
                info.enable_rtcp = data["enable_rtcp"].get<int>() != 0;
            }
        }
        if (data.contains("local_ip") && data["local_ip"].is_string()) {
            info.local_ip = data["local_ip"].get<std::string>();
        }
        if (data.contains("ssrc") && data["ssrc"].is_string()) {
            info.ssrc = data["ssrc"].get<std::string>();
        }
        
        if (info.port > 0) {
            LOG_INFO("创建RTP服务器成功: stream_id={}, port={}, tcp_mode={}", 
                     stream_id, info.port, info.tcp_mode);
        } else {
            LOG_WARN("创建RTP服务器返回成功但port为0: stream_id={}, response={}", 
                     stream_id, response.dump());
        }
    } else {
        std::string msg;
        if (response.contains("msg")) {
            msg = response["msg"].get<std::string>();
        }
        // 记录完整的响应信息以便调试
        std::string response_str = response.dump();
        LOG_ERROR("创建RTP服务器失败: stream_id={}, msg={}, response={}", 
                 stream_id, msg, response_str);
    }
    
    return info;
}

bool ZLMClient::CloseRtpServer(
    const std::string& stream_id,
    const std::string& app,
    const std::string& vhost) {
    
    json params = {
        {"secret", secret_},
        {"stream_id", stream_id},
        {"app", app},
        {"vhost", vhost}
    };
    
    auto response = PostRequest("/index/api/closeRtpServer", params);
    bool success = false;
    
    if (response.contains("code")) {
        if (response["code"].is_number()) {
            success = response["code"].get<int>() == 0;
        } else if (response["code"].is_string()) {
            success = response["code"].get<std::string>() == "0";
        }
    }
    
    if (success) {
        LOG_INFO("关闭RTP服务器成功: stream_id={}, app={}, vhost={}", 
                 stream_id, app, vhost);
    } else {
        std::string msg;
        if (response.contains("msg")) {
            msg = response["msg"].get<std::string>();
        }
        LOG_ERROR("关闭RTP服务器失败: stream_id={}, msg={}", stream_id, msg);
    }
    
    return success;
}

std::vector<RtpServerInfo> ZLMClient::ListRtpServer() {
    json params = {
        {"secret", secret_}
    };
    
    auto response = Request("/index/api/listRtpServer", params);
    std::vector<RtpServerInfo> servers;
    
    if (!response.contains("code")) {
        return servers;
    }
    
    bool success = false;
    if (response["code"].is_number()) {
        success = response["code"].get<int>() == 0;
    } else if (response["code"].is_string()) {
        success = response["code"].get<std::string>() == "0";
    }
    
    if (success && response.contains("data") && response["data"].is_array()) {
        for (const auto& item : response["data"]) {
            RtpServerInfo info;
            if (item.contains("stream_id") && item["stream_id"].is_string()) {
                info.stream_id = item["stream_id"].get<std::string>();
            }
            if (item.contains("port") && item["port"].is_number()) {
                info.port = item["port"].get<int>();
            }
            if (item.contains("tcp_mode") && item["tcp_mode"].is_number()) {
                info.tcp_mode = item["tcp_mode"].get<int>();
            }
            if (item.contains("enable_rtcp")) {
                if (item["enable_rtcp"].is_boolean()) {
                    info.enable_rtcp = item["enable_rtcp"].get<bool>();
                } else if (item["enable_rtcp"].is_number()) {
                    info.enable_rtcp = item["enable_rtcp"].get<int>() != 0;
                }
            }
            if (item.contains("local_ip") && item["local_ip"].is_string()) {
                info.local_ip = item["local_ip"].get<std::string>();
            }
            if (item.contains("ssrc") && item["ssrc"].is_string()) {
                info.ssrc = item["ssrc"].get<std::string>();
            }
            servers.push_back(info);
        }
    }
    
    return servers;
}

RtpServerInfo ZLMClient::GetRtpServerInfo(
    const std::string& stream_id,
    const std::string& app,
    const std::string& vhost) {
    
    // 先获取所有RTP服务器，然后查找匹配的
    auto servers = ListRtpServer();
    for (const auto& server : servers) {
        if (server.stream_id == stream_id) {
            return server;
        }
    }
    
    // 未找到，返回空对象
    RtpServerInfo info;
    info.stream_id = stream_id;
    return info;
}

RtpInfo ZLMClient::GetRtpInfo(
    const std::string& stream_id,
    const std::string& app,
    const std::string& vhost) {
    
    json params = {
        {"secret", secret_},
        {"stream_id", stream_id},
        {"app", app},
        {"vhost", vhost}
    };
    
    auto response = Request("/index/api/getRtpInfo", params);
    RtpInfo info;
    
    bool success = false;
    if (response.contains("code")) {
        if (response["code"].is_number()) {
            success = response["code"].get<int>() == 0;
        } else if (response["code"].is_string()) {
            success = response["code"].get<std::string>() == "0";
        }
    }
    
    if (success && response.contains("data")) {
        const auto& data = response["data"];
        if (data.contains("peer_ip") && data["peer_ip"].is_string()) {
            info.peer_ip = data["peer_ip"].get<std::string>();
        }
        if (data.contains("peer_port") && data["peer_port"].is_number()) {
            info.peer_port = data["peer_port"].get<int>();
        }
        if (data.contains("local_ip") && data["local_ip"].is_string()) {
            info.local_ip = data["local_ip"].get<std::string>();
        }
        if (data.contains("local_port") && data["local_port"].is_number()) {
            info.local_port = data["local_port"].get<int>();
        }
    }
    
    return info;
}

bool ZLMClient::UpdateRtpServerSSRC(
    const std::string& stream_id,
    const std::string& ssrc,
    const std::string& app,
    const std::string& vhost) {
    
    json params = {
        {"secret", secret_},
        {"stream_id", stream_id},
        {"ssrc", ssrc},
        {"app", app},
        {"vhost", vhost}
    };
    
    auto response = PostRequest("/index/api/updateRtpServerSSRC", params);
    bool success = false;
    
    if (response.contains("code")) {
        if (response["code"].is_number()) {
            success = response["code"].get<int>() == 0;
        } else if (response["code"].is_string()) {
            success = response["code"].get<std::string>() == "0";
        }
    }
    
    if (success) {
        LOG_INFO("更新RTP服务器SSRC成功: stream_id={}, ssrc={}", stream_id, ssrc);
    } else {
        std::string msg;
        if (response.contains("msg")) {
            msg = response["msg"].get<std::string>();
        }
        LOG_ERROR("更新RTP服务器SSRC失败: stream_id={}, msg={}", stream_id, msg);
    }
    
    return success;
}

json ZLMClient::PostRequest(const std::string& api_path, const json& params) {
    // 每次请求创建新的 CURL 实例
    CURL* curl = curl_easy_init();
    if (!curl) {
        LOG_ERROR("创建 CURL 实例失败");
        return json{{"code", -1}, {"msg", "Failed to create CURL instance"}};
    }
    
    std::string response_data;
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response_data);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 10L);  // POST请求可能需要更长时间
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 2L);
    
    // 构建 URL
    std::string url = api_url_ + api_path;
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    
    // 设置POST请求
    curl_easy_setopt(curl, CURLOPT_POST, 1L);
    
    // 构建JSON body
    std::string json_data = params.dump();
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, json_data.c_str());
    curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, json_data.length());
    
    // 设置HTTP头
    struct curl_slist* headers = nullptr;
    headers = curl_slist_append(headers, "Content-Type: application/json");
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    
    // 执行请求
    CURLcode res = curl_easy_perform(curl);
    
    // 清理
    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);
    
    if (res != CURLE_OK) {
        LOG_ERROR("CURL POST 请求失败: {}", curl_easy_strerror(res));
        return json{{"code", -1}, {"msg", curl_easy_strerror(res)}};
    }
    
    // 解析响应
    try {
        return json::parse(response_data);
    } catch (const json::exception& e) {
        LOG_ERROR("解析 JSON 响应失败: {}", e.what());
        return json{{"code", -1}, {"msg", "JSON parse error"}};
    }
}

} // namespace streaming
