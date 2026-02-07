#include "stream_manager.hpp"
#include "api/websocket_server.hpp"
#include "gateway/utils/error_codes.hpp"
#include "utils/logger.hpp"
#include <algorithm>
#include <chrono>
#include <errno.h>
#include <fstream>
#include <nlohmann/json.hpp>
#include <set>
#include <signal.h>
#include <sys/types.h>
#include <thread>
#include <unistd.h>

namespace streaming {

StreamManager::StreamManager(
    std::shared_ptr<ZLMClient> zlm_client,
    std::shared_ptr<api::WebSocketServer> websocket_server)
    : zlm_client_(zlm_client), websocket_server_(websocket_server) {

  // 初始化GB28181流名称匹配器（用于优化流名称匹配性能）
  gb28181_matcher_ = std::make_unique<gateway::gb28181::GB28181StreamMatcher>();

  LOG_INFO("Stream Manager 初始化完成");
}

StreamManager::~StreamManager() {
  StopStatusSync();
  LOG_INFO("Stream Manager 析构完成");
}

std::string StreamManager::GetStreamKey(const std::string &app,
                                        const std::string &stream) const {
  return app + "/" + stream;
}

/**
 * @brief 将 output_protocol 映射到 ZLM 的 schema
 * @return ZLM 的 schema，如果不支持则返回空字符串
 */
static std::string OutputProtocolToSchema(const std::string &output_protocol) {
  if (output_protocol == "http-flv")
    return "http-flv";
  if (output_protocol == "hls")
    return "hls";
  if (output_protocol == "rtmp")
    return "rtmp";
  if (output_protocol == "rtsp")
    return "rtsp";
  if (output_protocol == "webrtc")
    return ""; // WebRTC不通过getMediaList查询
  return "";
}

// ============================================
// 私有辅助函数实现
// ============================================

int64_t StreamManager::GetCurrentTimestamp() {
  return std::chrono::duration_cast<std::chrono::seconds>(
             std::chrono::system_clock::now().time_since_epoch())
      .count();
}

bool StreamManager::IsGatewayRegistered(const StreamMetadata &metadata) {
  return !metadata.gateway_type.empty() && metadata.gateway_type != "native";
}

bool StreamManager::IsAutoRegistered(const StreamMetadata &existing) {
  return (existing.gateway_type == "native" || existing.gateway_type.empty()) &&
         existing.protocol != "local-camera" && existing.source_url.empty();
}

std::string
StreamManager::InferProtocolFromSourceUrl(const std::string &source_url) {
  if (source_url.empty())
    return "";

  if (source_url.find("rtsp://") == 0)
    return "rtsp";
  if (source_url.find("rtmp://") == 0)
    return "rtmp";
  if (source_url.find("http://") == 0 || source_url.find("https://") == 0) {
    if (source_url.find(".m3u8") != std::string::npos)
      return "hls";
    if (source_url.find(".flv") != std::string::npos)
      return "http-flv";
    if (source_url.find(".mpd") != std::string::npos)
      return "dash";
    return "http";
  }

  return "";
}

void StreamManager::UpdateMetadataFields(StreamMetadata &existing,
                                         const StreamMetadata &metadata,
                                         bool preserve_protocols) {
  // 协议字段：根据 preserve_protocols 标志决定是否更新
  if (!preserve_protocols) {
    if (!metadata.protocol.empty() && existing.protocol.empty()) {
      existing.protocol = metadata.protocol;
    }
    if (!metadata.output_protocol.empty() && existing.output_protocol.empty()) {
      existing.output_protocol = metadata.output_protocol;
    }
  }

  // 其他字段：总是更新（如果提供）
  if (!metadata.source_url.empty()) {
    existing.source_url = metadata.source_url;
  }
  if (!metadata.device_id.empty()) {
    existing.device_id = metadata.device_id;
  }
  if (!metadata.device_type.empty()) {
    existing.device_type = metadata.device_type;
  }
  if (!metadata.gateway_type.empty()) {
    existing.gateway_type = metadata.gateway_type;
  }
  if (metadata.pid > 0) {
    existing.pid = metadata.pid;
  }
  if (!metadata.processing_type.empty()) {
    existing.processing_type = metadata.processing_type;
  }
}

void StreamManager::BroadcastStreamUpdate(StreamMetadata &metadata,
                                          bool removed, bool force) {
  if (!websocket_server_) {
    return;
  }

  // 节流逻辑：
  // 1. 如果是删除事件 (removed=true)，必须广播
  // 2. 如果是强制广播 (force=true)，例如状态变化，必须广播
  // 3. 否则，检查通过时间间隔（1000ms）

  // 如果是秒级时间戳，GetCurrentTimestamp() 返回秒，乘以1000得到毫秒
  // 注意：StreamMetadata 中的 last_broadcast_time 已改为毫秒

  // 获取毫秒级当前时间
  int64_t now_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                       std::chrono::system_clock::now().time_since_epoch())
                       .count();

  if (!removed && !force) {
    if (now_ms - metadata.last_broadcast_time < 1000) {
      // 距离上次广播不足1秒，且非关键更新，跳过
      return;
    }
  }

  metadata.last_broadcast_time = now_ms;

  nlohmann::json json_data;
  json_data["app"] = metadata.app;
  json_data["stream"] = metadata.stream;
  json_data["status"] = static_cast<int>(metadata.status);

  // 包含统计信息
  json_data["zlm_alive"] = metadata.zlm_alive;
  json_data["reader_count"] = metadata.reader_count;
  json_data["bytes_speed"] = metadata.bytes_speed;
  json_data["total_bytes"] = metadata.total_bytes;

  if (removed) {
    json_data["removed"] = true;
  }

  // 添加转码原因（如果有）
  if (!metadata.transcoding_reason.empty()) {
    json_data["transcoding_reason"] = metadata.transcoding_reason;
  }
  // 可以添加更多字段
  LOG_INFO("Broadcasting stream update: app={}, stream={}, status={}",
           metadata.app, metadata.stream, (int)metadata.status);
  websocket_server_->BroadcastStreamUpdate(json_data);
}

void StreamManager::RecordStatusTransition(const std::string &app,
                                           const std::string &stream,
                                           StreamStatus from_status,
                                           StreamStatus to_status,
                                           const std::string &source) {
  if (from_status == to_status) {
    return; // 状态未变化，不记录
  }

  try {
    std::lock_guard<std::mutex> lock(stats_mutex_);

    // 更新转换统计
    if (source == "gateway_create_requested") {
      status_stats_.transitions.gateway_create_requested++;
    } else if (source == "gateway_create_result") {
      status_stats_.transitions.gateway_create_result++;
    } else if (source == "zlm_state_update") {
      status_stats_.transitions.zlm_state_update++;
    } else if (source == "sync_polling") {
      status_stats_.transitions.sync_polling++;
    } else if (source == "manual_update") {
      status_stats_.transitions.manual_update++;
    }

    // 记录最近的状态转换
    StatusStatistics::TransitionDetail detail;
    detail.app = app;
    detail.stream = stream;
    detail.from_status = StreamStatusToString(from_status);
    detail.to_status = StreamStatusToString(to_status);
    detail.source = source;
    detail.timestamp = GetCurrentTimestamp();

    status_stats_.recent_transitions.push_back(detail);

    // 保持最近转换记录不超过最大值
    if (status_stats_.recent_transitions.size() >
        StatusStatistics::MAX_RECENT_TRANSITIONS) {
      status_stats_.recent_transitions.erase(
          status_stats_.recent_transitions.begin());
    }
  } catch (const std::exception &e) {
    LOG_ERROR("[RecordStatusTransition] Exception: {} (app={}, stream={})",
              e.what(), app, stream);
  }
}

std::string StreamManager::StreamStatusToString(StreamStatus status) const {
  switch (status) {
  case StreamStatus::Stopped:
    return "Stopped";
  case StreamStatus::Starting:
    return "Starting";
  case StreamStatus::Running:
    return "Running";
  case StreamStatus::Stopping:
    return "Stopping";
  case StreamStatus::Error:
    return "Error";
  default:
    return "Unknown";
  }
}

bool StreamManager::RegisterStream(const StreamMetadata &metadata) {
  std::lock_guard<std::mutex> lock(mutex_);
  std::string key = GetStreamKey(metadata.app, metadata.stream);

  if (streams_.count(key) > 0) {
    LOG_WARN("流已存在，更新元数据: {}", key);
    // 更新现有流的元数据，但保留关键字段
    StreamMetadata &existing = streams_[key];

    // 保留原始创建时间
    int64_t original_create_time = existing.create_time;

    // 如果新注册的流有 gateway_type（说明是通过 Gateway
    // 注册的），优先使用新流的协议信息 这样可以确保本地摄像头等通过 Gateway
    // 注册的流不会被自动注册的流覆盖
    bool is_gateway_registered = IsGatewayRegistered(metadata);
    bool is_auto_registered = IsAutoRegistered(existing);

    // 如果新流是通过 Gateway 注册的，且现有流是自动注册的，则更新协议信息
    if (is_gateway_registered && is_auto_registered) {
      existing.protocol = metadata.protocol;
      existing.output_protocol = metadata.output_protocol;
      existing.gateway_type = metadata.gateway_type;
      existing.device_type = metadata.device_type;
      existing.device_id = metadata.device_id;
      if (!metadata.source_url.empty()) {
        existing.source_url = metadata.source_url;
      }
      LOG_INFO("更新自动注册的流为 Gateway 注册的流: {} (protocol: {} -> {})",
               key, existing.protocol, metadata.protocol);
    } else {
      // 只更新提供的非空字段，但关键字段只有在当前为空时才更新，避免覆盖
      // protocol 和 output_protocol 是创建流时指定的，不应该在流已存在时被覆盖
      UpdateMetadataFields(existing, metadata,
                           true); // preserve_protocols = true
    }

    // 更新状态和时间戳
    // 注意：只有在新状态比现有状态"更好"时才更新状态
    // Running > Starting > Stopped > Error
    // 这样可以避免状态被错误地降级（例如从 Running 降级到 Starting）
    if (metadata.status == StreamStatus::Running) {
      // 新状态是 Running，总是更新（即使现有状态也是 Running，也要更新时间戳）
      existing.status = StreamStatus::Running;
      existing.last_update_time = GetCurrentTimestamp();
    } else if (metadata.status == StreamStatus::Starting &&
               existing.status != StreamStatus::Running) {
      // 新状态是 Starting，只有在现有状态不是 Running 时才更新
      existing.status = StreamStatus::Starting;
      existing.last_update_time = GetCurrentTimestamp();
    } else if (metadata.status == StreamStatus::Error &&
               existing.status == StreamStatus::Error) {
      // 新状态是 Error，只有在现有状态也是 Error 时才更新时间戳（不改变状态）
      existing.last_update_time = GetCurrentTimestamp();
    } else if (metadata.status == StreamStatus::Stopped &&
               existing.status != StreamStatus::Running &&
               existing.status != StreamStatus::Starting) {
      // 新状态是 Stopped，只有在现有状态不是 Running 或 Starting 时才更新
      existing.status = StreamStatus::Stopped;
      existing.last_update_time = GetCurrentTimestamp();
    }
    // 其他情况不更新状态，避免状态被错误降级
    existing.create_time = original_create_time; // 保留原始创建时间

    // 广播更新 (强制更新，因为可能是新流更新)
    BroadcastStreamUpdate(existing, false, true);

    return true;
  }

  StreamMetadata new_metadata = metadata;
  auto now = GetCurrentTimestamp();

  if (new_metadata.create_time == 0) {
    new_metadata.create_time = now;
  }
  new_metadata.last_update_time = now;

  streams_[key] = new_metadata;
  LOG_INFO("注册流: {} (协议: {}, Gateway: {})", key, metadata.protocol,
           metadata.gateway_type);

  // 广播更新 (新流，强制)
  BroadcastStreamUpdate(new_metadata, false, true);

  return true;
}

bool StreamManager::UnregisterStream(const std::string &app,
                                     const std::string &stream) {
  std::lock_guard<std::mutex> lock(mutex_);
  std::string key = GetStreamKey(app, stream);
  std::string original_key = key;

  auto it = streams_.find(key);
  if (it == streams_.end()) {
    // 对于GB28181流，如果stream包含时间戳，尝试去掉时间戳后查找
    if (app == "gb28181" && stream.find('_') != std::string::npos) {
      // 使用gb28181_matcher_进行O(1)匹配
      if (gb28181_matcher_) {
        std::string base_name = gb28181_matcher_->GetKey(stream).base_name;
        if (base_name != stream) {
          std::string base_key = GetStreamKey(app, base_name);
          it = streams_.find(base_key);
          if (it != streams_.end()) {
            LOG_DEBUG(
                "UnregisterStream: GB28181 stream matched (O(1)): {} -> {}",
                stream, base_name);
            key = base_key; // 更新key为实际找到的key
          }
        }
      } else {
        // 回退到旧逻辑（不应该发生，除非初始化失败）
        LOG_WARN("gb28181_matcher_ not initialized!");
      }
    }

    if (it == streams_.end()) {
      LOG_WARN("注销流失败，流不存在: {}", key);

      // 即便流不存在，也把它加入手动停止列表
      // 防止ZLM稍微延迟的hook通知又把流注册回来
      manually_stopped_streams_.insert(original_key);

      // 尝试记录基础名称
      if (app == "gb28181" && stream.find('_') != std::string::npos &&
          gb28181_matcher_) {
        std::string base_name = gb28181_matcher_->GetKey(stream).base_name;
        if (base_name != stream) {
          std::string base_key = GetStreamKey(app, base_name);
          manually_stopped_streams_.insert(base_key);
        }
      }

      return false;
    }
  }

  // 记录到手动停止列表
  manually_stopped_streams_.insert(key);
  if (key != original_key) {
    manually_stopped_streams_.insert(original_key);
  }

  // 创建临时元数据用于广播（状态设为 Stopped）
  StreamMetadata temp_metadata = it->second;
  temp_metadata.status = StreamStatus::Stopped;

  streams_.erase(it);
  LOG_INFO("注销流: {} (已加入手动停止列表)", key);

  // 广播更新 (删除，强制)
  BroadcastStreamUpdate(temp_metadata, true, true);

  // 对于GB28181，如果使用了带时间戳的名称，额外发送一个广播
  if (app == "gb28181" && key != original_key) {
    StreamMetadata timestamp_metadata = temp_metadata;
    timestamp_metadata.stream = stream; // 使用原始带时间戳的名称
    BroadcastStreamUpdate(timestamp_metadata, true, true);
  }

  return true;
}

bool StreamManager::UpdateStreamStatus(const std::string &app,
                                       const std::string &stream,
                                       StreamStatus status) {
  std::lock_guard<std::mutex> lock(mutex_);
  std::string key = GetStreamKey(app, stream);

  auto it = streams_.find(key);
  if (it == streams_.end()) {
    LOG_WARN("流不存在，无法更新状态: {}", key);
    return false;
  }

  StreamStatus old_status = it->second.status;
  it->second.status = status;
  it->second.last_update_time = GetCurrentTimestamp();

  // 记录状态转换（运维通道）
  if (old_status != status) {
    RecordStatusTransition(app, stream, old_status, status, "manual_update");
    LOG_WARN("UpdateStreamStatus (运维通道): 强制状态转移 {} -> {}: {}",
             static_cast<int>(old_status), static_cast<int>(status), key);
  }

  LOG_DEBUG("更新流状态: {} -> {}", key, static_cast<int>(status));

  // 广播更新 (手动状态更新，强制)
  BroadcastStreamUpdate(it->second, false, true);

  return true;
}

void StreamManager::OnStreamCreateRequested(const StreamMetadata &metadata) {
  // 统一入口：Gateway 发起创建流时调用
  std::lock_guard<std::mutex> lock(mutex_);
  std::string key = GetStreamKey(metadata.app, metadata.stream);

  // #region agent log
  {
    std::ofstream log_file("logs/debug_agent.log", std::ios::app);
    if (log_file.is_open()) {
      nlohmann::json log_entry = {
          {"timestamp", std::chrono::duration_cast<std::chrono::milliseconds>(
                            std::chrono::system_clock::now().time_since_epoch())
                            .count()},
          {"location", "stream_manager.cpp:372"},
          {"message", "OnStreamCreateRequested entry"},
          {"data",
           {{"app", metadata.app},
            {"stream", metadata.stream},
            {"protocol", metadata.protocol},
            {"gateway_type", metadata.gateway_type}}},
          {"sessionId", "debug-session"},
          {"runId", "run1"},
          {"hypothesisId", "D"}};
      log_file << log_entry.dump() << "\n";
    }
  }
  // #endregion

  auto it = streams_.find(key);
  auto now = GetCurrentTimestamp();

  if (it == streams_.end()) {
    // 新流：以 Starting 状态注册一条记录
    StreamMetadata new_metadata = metadata;
    new_metadata.status = StreamStatus::Starting;
    if (new_metadata.create_time == 0) {
      new_metadata.create_time = now;
    }
    new_metadata.last_update_time = now;

    streams_[key] = new_metadata;
    LOG_INFO("OnStreamCreateRequested: 注册新流为 Starting: {}", key);
    RecordStatusTransition(metadata.app, metadata.stream, StreamStatus::Stopped,
                           StreamStatus::Starting, "gateway_create_requested");
    BroadcastStreamUpdate(new_metadata, false, true);
  } else {
    // 已存在：状态转移规则（根据文档 2.4.2）：
    // - Stopped → Starting（允许）
    // - Error → Starting（允许，表示重试）
    // - Starting → Starting（允许，更新元数据和时间戳）
    // - Running → Starting（不允许降级，保持 Running）
    StreamMetadata &existing = it->second;
    StreamStatus old_status = existing.status;

    // 补齐协议信息等（保持与 RegisterStream 的语义一致）
    UpdateMetadataFields(existing, metadata,
                         false /* preserve_protocols = false */);

    if (old_status == StreamStatus::Stopped ||
        old_status == StreamStatus::Error ||
        old_status == StreamStatus::Starting) {
      // 允许转移到 Starting
      existing.status = StreamStatus::Starting;
      existing.last_update_time = now;
      LOG_INFO(
          "OnStreamCreateRequested: 状态转移 {} -> Starting (创建请求): {}",
          static_cast<int>(old_status), key);
      RecordStatusTransition(metadata.app, metadata.stream, old_status,
                             StreamStatus::Starting,
                             "gateway_create_requested");
    } else if (old_status == StreamStatus::Running) {
      // 不允许从 Running 降级到 Starting，保持 Running 状态
      existing.last_update_time = now;
      LOG_WARN("OnStreamCreateRequested: 检测到状态降级尝试 Running -> "
               "Starting: {}，"
               "保持 Running 状态避免降级",
               key);
    } else {
      // 未知状态，记录告警但允许更新
      LOG_WARN(
          "OnStreamCreateRequested: 检测到未知状态 {}，允许转移到 Starting: {}",
          static_cast<int>(old_status), key);
      existing.status = StreamStatus::Starting;
      existing.last_update_time = now;
      RecordStatusTransition(metadata.app, metadata.stream, old_status,
                             StreamStatus::Starting,
                             "gateway_create_requested");
    }

    BroadcastStreamUpdate(existing, false, true);
  }
}

void StreamManager::OnStreamCreateResult(const std::string &app,
                                         const std::string &stream,
                                         bool success,
                                         const std::string &error_code,
                                         const std::string &error_message) {
  std::lock_guard<std::mutex> lock(mutex_);
  std::string key = GetStreamKey(app, stream);

  // #region agent log
  {
    std::ofstream log_file("logs/debug_agent.log", std::ios::app);
    if (log_file.is_open()) {
      nlohmann::json log_entry = {
          {"timestamp", std::chrono::duration_cast<std::chrono::milliseconds>(
                            std::chrono::system_clock::now().time_since_epoch())
                            .count()},
          {"location", "stream_manager.cpp:433"},
          {"message", "OnStreamCreateResult entry"},
          {"data",
           {{"app", app},
            {"stream", stream},
            {"success", success},
            {"error_code", error_code},
            {"error_message", error_message}}},
          {"sessionId", "debug-session"},
          {"runId", "run1"},
          {"hypothesisId", "D"}};
      log_file << log_entry.dump() << "\n";
    }
  }
  // #endregion

  auto it = streams_.find(key);
  if (it == streams_.end()) {
    LOG_WARN("OnStreamCreateResult: 流不存在，无法更新创建结果: {}", key);
    return;
  }

  StreamMetadata &existing = it->second;
  auto now = GetCurrentTimestamp();
  StreamStatus old_status = existing.status;

  // 状态转移规则（根据文档 2.4.2）：
  // - Starting → Running（success == true）
  // - Starting → Error（success == false）
  // - 如果已经是 Running，不允许降级到 Error（避免误伤已由 Hook 提升为 Running
  // 的流）

  if (success) {
    // 创建成功：Starting → Running
    if (old_status == StreamStatus::Starting) {
      existing.status = StreamStatus::Running;
      existing.last_update_time = now;
      LOG_INFO(
          "OnStreamCreateResult: 状态转移 Starting -> Running (创建成功): {}",
          key);
      RecordStatusTransition(app, stream, old_status, StreamStatus::Running,
                             "gateway_create_result");
    } else if (old_status == StreamStatus::Running) {
      // 已经是 Running，只更新时间戳
      existing.last_update_time = now;
      LOG_DEBUG("OnStreamCreateResult: 创建成功，但流已处于 Running 状态: {}",
                key);
    } else {
      // 从其他状态转移到 Running（可能是状态机时序问题，记录告警但允许）
      LOG_WARN("OnStreamCreateResult: 检测到非预期的状态转移 {} -> Running "
               "(创建成功): {}，"
               "允许更新但请检查状态机时序",
               static_cast<int>(old_status), key);
      existing.status = StreamStatus::Running;
      existing.last_update_time = now;
      RecordStatusTransition(app, stream, old_status, StreamStatus::Running,
                             "gateway_create_result");
    }
  } else {
    // 创建失败：Starting → Error
    if (old_status == StreamStatus::Starting) {
      existing.status = StreamStatus::Error;
      existing.last_update_time = now;
      // 保存错误信息
      if (!error_code.empty()) {
        existing.error_code = error_code;
      }
      if (!error_message.empty()) {
        existing.error_message = error_message;
      }
      LOG_WARN("OnStreamCreateResult: 状态转移 Starting -> Error (创建失败): "
               "{} (错误码: {}, 错误消息: {})",
               key, error_code.empty() ? "N/A" : error_code,
               error_message.empty() ? "N/A" : error_message);
      RecordStatusTransition(app, stream, old_status, StreamStatus::Error,
                             "gateway_create_result");
    } else if (old_status == StreamStatus::Running) {
      // 已经是 Running，不允许降级到 Error（避免误伤已由 Hook 提升为 Running
      // 的流）
      existing.last_update_time = now;
      LOG_WARN("OnStreamCreateResult: 创建失败，但流已处于 Running 状态，保持 "
               "Running 避免降级: {}",
               key);
    } else if (old_status == StreamStatus::Error) {
      // 已经是 Error，只更新时间戳
      existing.last_update_time = now;
      LOG_DEBUG("OnStreamCreateResult: 创建失败，流已处于 Error 状态: {}", key);
    } else {
      // 从其他状态转移到 Error（可能是状态机时序问题，记录告警但允许）
      LOG_WARN("OnStreamCreateResult: 检测到非预期的状态转移 {} -> Error "
               "(创建失败): {}，"
               "允许更新但请检查状态机时序",
               static_cast<int>(old_status), key);
      existing.status = StreamStatus::Error;
      existing.last_update_time = now;
      RecordStatusTransition(app, stream, old_status, StreamStatus::Error,
                             "gateway_create_result");
    }
  }

  BroadcastStreamUpdate(existing, false, true);
}

void StreamManager::OnZLMStreamState(const std::string &app,
                                     const std::string &stream, bool regist,
                                     bool alive, int reader_count,
                                     int64_t bytes_speed, int64_t total_bytes) {
  std::lock_guard<std::mutex> lock(mutex_);
  std::string key = GetStreamKey(app, stream);

  auto it = streams_.find(key);
  if (it == streams_.end()) {
    // 对于暂未在 StreamManager 注册的流，检查是否在手动停止列表中
    // 如果在手动停止列表中，忽略状态更新（防止重新注册）
    if (manually_stopped_streams_.count(key) > 0) {
      LOG_DEBUG("OnZLMStreamState: "
                "流在手动停止列表中，忽略状态更新（防止重新注册）: {}",
                key);
      return;
    }

    // 对于GB28181流，如果stream包含时间戳，尝试去掉时间戳后检查
    if (app == "gb28181" && stream.find('_') != std::string::npos) {
      size_t last_underscore = stream.find_last_of('_');
      if (last_underscore != std::string::npos &&
          last_underscore < stream.length() - 1) {
        std::string possible_timestamp = stream.substr(last_underscore + 1);
        bool is_timestamp = !possible_timestamp.empty() &&
                            possible_timestamp.length() >= 10 &&
                            std::all_of(possible_timestamp.begin(),
                                        possible_timestamp.end(), ::isdigit);
        if (is_timestamp) {
          std::string base_stream = stream.substr(0, last_underscore);
          std::string base_key = GetStreamKey(app, base_stream);
          if (manually_stopped_streams_.count(base_key) > 0) {
            LOG_DEBUG("OnZLMStreamState: "
                      "流（基础名称）在手动停止列表中，忽略状态更新（防止重新注"
                      "册）: {} -> {}",
                      stream, base_key);
            return;
          }
        }
      }
    }

    // 对于暂未在 StreamManager 注册的流，当前阶段只记录日志，不自动注册，
    // 保持与原有 Hook 行为一致（由重试机制和 SyncZLMStatus 负责兜底）。
    LOG_DEBUG(
        "OnZLMStreamState: 流尚未在 StreamManager 中注册，忽略状态更新: {}",
        key);
    return;
  }

  StreamMetadata &metadata = it->second;
  auto now = GetCurrentTimestamp();

  // 更新 ZLM 统计信息
  metadata.zlm_alive = alive;
  metadata.reader_count = reader_count;
  metadata.bytes_speed = bytes_speed;
  metadata.total_bytes = total_bytes;

  // 更新 ZLM 实际的 app 和 stream 名称（用于 GB28181 等带时间戳的流）
  // 只有当传入的 stream 与 metadata.stream 不同（例如包含时间戳）时才更新
  if (stream != metadata.stream) {
    metadata.zlm_app = app;
    metadata.zlm_stream = stream;
    LOG_DEBUG("OnZLMStreamState: 更新 ZLM 实际流名称: {}/{} -> {}/{}",
              metadata.app, metadata.stream, app, stream);
  } else if (metadata.zlm_stream.empty()) {
    // 如果 zlm_stream 为空，初始化为当前 stream
    metadata.zlm_app = app;
    metadata.zlm_stream = stream;
  }

  // 状态转移规则（根据文档 2.4.2）：
  // - regist && alive -> Running（允许从 Starting/Error/Stopped 转移到
  // Running）
  // - !regist 或 !alive -> Stopped（允许从 Running/Starting 转移到 Stopped）
  // 但是，如果流刚启动成功（从 Starting 变为 Running
  // 的时间很短），应该更谨慎地处理状态转移 避免在流刚启动时因为 ZLM Hook
  // 的时序问题导致状态被误设为 Stopped

  // 重要优化：即使ZLM的alive字段为false，如果有数据传输（bytes_speed>0或total_bytes>0），
  // 也应该认为流是活跃的。这是因为ZLM的alive字段可能有延迟，或者在某些情况下不准确。
  // 这与ZLMClient::IsStreamActive的逻辑保持一致。
  bool effective_alive = alive || (bytes_speed > 0) || (total_bytes > 0);

  if (effective_alive) {
    metadata.zlm_last_alive_time = now;
  }

  StreamStatus old_status = metadata.status;
  StreamStatus new_status;

  // 计算从上次状态更新到现在的时间（秒）
  int64_t time_since_update = now - metadata.last_update_time;

  // 计算从流创建到现在的时间（秒）
  int64_t time_since_create = now - metadata.create_time;

  // 如果流刚变为 Running（30秒内），且当前状态是 Running，即使 ZLM 上报
  // regist=false 或 alive=false， 也应该保持 Running 状态，因为可能是 ZLM Hook
  // 的时序问题（流刚启动，ZLM 还没完全注册）
  bool is_recently_running =
      (old_status == StreamStatus::Running) && (time_since_update < 30);

  // 对于Starting状态的流，如果regist=true但alive=false，但流刚创建（60秒内），
  // 应该等待一段时间，因为ZLM的alive字段可能有延迟
  bool is_recently_created =
      (old_status == StreamStatus::Starting) && (time_since_create < 60);

  // #region agent log
  {
    std::ofstream log_file("logs/debug_agent.log", std::ios::app);
    if (log_file.is_open()) {
      nlohmann::json log_entry = {
          {"timestamp", std::chrono::duration_cast<std::chrono::milliseconds>(
                            std::chrono::system_clock::now().time_since_epoch())
                            .count()},
          {"location", "stream_manager.cpp:594"},
          {"message", "OnZLMStreamState status calculation"},
          {"data",
           {{"app", app},
            {"stream", stream},
            {"old_status", static_cast<int>(old_status)},
            {"regist", regist},
            {"alive", alive},
            {"effective_alive", effective_alive},
            {"bytes_speed", bytes_speed},
            {"total_bytes", total_bytes},
            {"time_since_create", time_since_create},
            {"time_since_update", time_since_update},
            {"is_recently_created", is_recently_created},
            {"is_recently_running", is_recently_running}}},
          {"sessionId", "debug-session"},
          {"runId", "run1"},
          {"hypothesisId", "D"}};
      log_file << log_entry.dump() << "\n";
    }
  }
  // #endregion

  if (regist && effective_alive) {
    new_status = StreamStatus::Running;
  } else if (regist && !effective_alive && is_recently_created) {
    // 流刚创建，regist=true但alive=false（可能是ZLM的alive字段延迟）
    // 保持Starting状态，等待alive字段变为true
    new_status = StreamStatus::Starting;
    LOG_DEBUG("OnZLMStreamState: "
              "流刚创建（{}秒前），regist=true但alive="
              "false，保持Starting状态等待alive字段更新: {} regist={} alive={} "
              "bytes_speed={} total_bytes={}",
              time_since_create, key, regist, alive, bytes_speed, total_bytes);
  } else {
    // 如果流刚启动成功，且当前状态是 Running，保持 Running 状态（避免误判）
    if (is_recently_running && old_status == StreamStatus::Running) {
      new_status = StreamStatus::Running;
      LOG_DEBUG("OnZLMStreamState: 流刚启动成功（{}秒前），保持 Running "
                "状态，避免因 ZLM Hook 时序问题导致误判: {} regist={} alive={}",
                time_since_update, key, regist, alive);
    } else {
      new_status = StreamStatus::Stopped;
    }
  }

  // #region agent log
  {
    std::ofstream log_file("logs/debug_agent.log", std::ios::app);
    if (log_file.is_open()) {
      nlohmann::json log_entry = {
          {"timestamp", std::chrono::duration_cast<std::chrono::milliseconds>(
                            std::chrono::system_clock::now().time_since_epoch())
                            .count()},
          {"location", "stream_manager.cpp:613"},
          {"message", "OnZLMStreamState status transition"},
          {"data",
           {{"app", app},
            {"stream", stream},
            {"old_status", static_cast<int>(old_status)},
            {"new_status", static_cast<int>(new_status)},
            {"status_changed", old_status != new_status}}},
          {"sessionId", "debug-session"},
          {"runId", "run1"},
          {"hypothesisId", "D"}};
      log_file << log_entry.dump() << "\n";
    }
  }
  // #endregion

  // 状态转移冲突检测和告警
  if (old_status != new_status) {
    // 检查是否为合法的状态转移
    bool valid_transition = false;
    std::string transition_reason;

    if (new_status == StreamStatus::Running) {
      // 允许从 Starting/Error/Stopped 转移到 Running
      if (old_status == StreamStatus::Starting ||
          old_status == StreamStatus::Error ||
          old_status == StreamStatus::Stopped) {
        valid_transition = true;
        transition_reason = "ZLM 上报流已注册且存活";
      } else if (old_status == StreamStatus::Running) {
        // 已经是 Running，只更新时间戳
        valid_transition = true;
        transition_reason = "ZLM 确认流仍在运行";
      }
    } else if (new_status == StreamStatus::Stopped) {
      // 允许从 Running/Starting 转移到 Stopped
      // 但是，如果流刚启动成功，不应该降级为 Stopped
      if (old_status == StreamStatus::Starting) {
        valid_transition = true;
        transition_reason = "ZLM 上报流未注册或未存活";
      } else if (old_status == StreamStatus::Running) {
        // 从 Running 转移到 Stopped：只有在流运行了一段时间后才允许（避免误判）
        if (time_since_update >= 30) {
          valid_transition = true;
          transition_reason = "ZLM 上报流未注册或未存活（流已运行超过30秒）";
        } else {
          // 流刚启动，不应该降级为 Stopped
          valid_transition = false;
          transition_reason = "流刚启动成功，保持 Running 状态，避免因 ZLM "
                              "Hook 时序问题导致误判";
          LOG_DEBUG("OnZLMStreamState: 流刚启动成功（{}秒前），拒绝降级 "
                    "Running -> Stopped: {} regist={} alive={}",
                    time_since_update, key, regist, alive);
        }
      } else if (old_status == StreamStatus::Stopped ||
                 old_status == StreamStatus::Error) {
        // 已经是 Stopped/Error，只更新时间戳
        valid_transition = true;
        transition_reason = "ZLM 确认流仍处于停止状态";
      }
    }

    if (valid_transition) {
      metadata.status = new_status;
      metadata.last_update_time = now;
      LOG_INFO(
          "OnZLMStreamState: 状态转移 {} -> {} ({}): {} regist={} alive={}",
          static_cast<int>(old_status), static_cast<int>(new_status),
          transition_reason, key, regist, alive);
      RecordStatusTransition(app, stream, old_status, new_status,
                             "zlm_state_update");
    } else {
      // 非法状态转移：记录告警但允许更新（避免阻塞）
      LOG_WARN("OnZLMStreamState: 检测到潜在的状态转移冲突 {} -> {} ({}): {} "
               "regist={} alive={}，"
               "允许更新但请检查状态机逻辑",
               static_cast<int>(old_status), static_cast<int>(new_status), key,
               regist, alive);
      metadata.status = new_status;
      metadata.last_update_time = now;
      RecordStatusTransition(app, stream, old_status, new_status,
                             "zlm_state_update");
    }
  } else {
    // 状态未变化，只更新时间戳
    metadata.last_update_time = now;
    LOG_DEBUG("OnZLMStreamState: 状态未变化 {}: {} regist={} alive={}",
              static_cast<int>(old_status), key, regist, alive);
  }

  // 只有状态变化时才强制广播，否则节流
  bool force_broadcast = (old_status != new_status);
  BroadcastStreamUpdate(metadata, false, force_broadcast);
}

bool StreamManager::UpdateStreamMetadata(const std::string &app,
                                         const std::string &stream,
                                         const StreamMetadata &metadata) {
  std::lock_guard<std::mutex> lock(mutex_);
  std::string key = GetStreamKey(app, stream);

  auto it = streams_.find(key);
  if (it == streams_.end()) {
    LOG_WARN("流不存在，无法更新元数据: {}", key);
    return false;
  }

  StreamMetadata &existing = it->second;

  // 只更新提供的字段（非空字段），保留协议信息
  UpdateMetadataFields(existing, metadata, true); // preserve_protocols = true

  existing.last_update_time = GetCurrentTimestamp();

  LOG_DEBUG("更新流元数据: {}", key);
  return true;
}

StreamMetadata
StreamManager::GetStreamMetadata(const std::string &app,
                                 const std::string &stream) const {
  std::lock_guard<std::mutex> lock(mutex_);
  std::string key = GetStreamKey(app, stream);

  auto it = streams_.find(key);
  if (it == streams_.end()) {
    // 对于GB28181流，如果stream包含时间戳，尝试去掉时间戳后查找
    // 这是关键修复：确保GetStreamMetadata也能处理带时间戳的流名称
    if (app == "gb28181" && stream.find('_') != std::string::npos) {
      size_t last_underscore = stream.find_last_of('_');
      if (last_underscore != std::string::npos &&
          last_underscore < stream.length() - 1) {
        std::string possible_timestamp = stream.substr(last_underscore + 1);
        bool is_timestamp = !possible_timestamp.empty() &&
                            possible_timestamp.length() >= 10 &&
                            std::all_of(possible_timestamp.begin(),
                                        possible_timestamp.end(), ::isdigit);
        if (is_timestamp) {
          std::string base_stream = stream.substr(0, last_underscore);
          std::string base_key = GetStreamKey(app, base_stream);
          it = streams_.find(base_key);
          if (it != streams_.end()) {
            LOG_DEBUG("GetStreamMetadata: 使用基础名称找到流: {} -> {}", key,
                      base_key);
            return it->second;
          }
        }
      }
    }

    // 如果还是找不到，返回空metadata
    StreamMetadata empty;
    empty.status = StreamStatus::Stopped;
    return empty;
  }

  return it->second;
}

void StreamManager::ForEachStream(
    std::function<bool(const StreamMetadata &)> callback) const {
  std::lock_guard<std::mutex> lock(mutex_);
  for (const auto &pair : streams_) {
    if (!callback(pair.second)) {
      break;
    }
  }
}

std::vector<StreamMetadata> StreamManager::GetAllStreams() const {
  std::lock_guard<std::mutex> lock(mutex_);
  std::vector<StreamMetadata> result;
  result.reserve(streams_.size());

  for (const auto &pair : streams_) {
    result.push_back(pair.second);
  }

  return result;
}

std::vector<StreamMetadata>
StreamManager::GetStreamsByProtocol(const std::string &protocol) const {
  std::lock_guard<std::mutex> lock(mutex_);
  std::vector<StreamMetadata> result;

  for (const auto &pair : streams_) {
    if (pair.second.protocol == protocol) {
      result.push_back(pair.second);
    }
  }

  return result;
}

std::vector<StreamMetadata>
StreamManager::GetStreamsByGatewayType(const std::string &gateway_type) const {
  std::lock_guard<std::mutex> lock(mutex_);
  std::vector<StreamMetadata> result;

  for (const auto &pair : streams_) {
    if (pair.second.gateway_type == gateway_type) {
      result.push_back(pair.second);
    }
  }

  return result;
}

bool StreamManager::StreamExists(const std::string &app,
                                 const std::string &stream) const {
  std::lock_guard<std::mutex> lock(mutex_);
  std::string key = GetStreamKey(app, stream);
  return streams_.count(key) > 0;
}

// ============================================
// Phase 2.1: 批量更新优化实现
// ============================================

std::vector<StreamManager::StreamUpdate> StreamManager::PrepareStreamUpdates(
    const std::map<std::string, StreamInfo> &zlm_stream_map, int64_t now) {

  auto start_time = std::chrono::steady_clock::now();

  std::vector<StreamUpdate> updates;
  updates.reserve(100); // 预分配

  // 1. 快照阶段（短暂持锁）
  auto snapshot_start = std::chrono::steady_clock::now();
  std::vector<std::pair<std::string, StreamMetadata>> snapshot;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    snapshot.reserve(streams_.size());
    for (const auto &[key, meta] : streams_) {
      snapshot.push_back({key, meta});
    }
  }
  auto snapshot_duration =
      std::chrono::duration_cast<std::chrono::microseconds>(
          std::chrono::steady_clock::now() - snapshot_start)
          .count();
  // 锁已释放

  LOG_DEBUG("Phase 2.1: Snapshot took {}μs (lock held)", snapshot_duration);

  // 清理常量
  static constexpr int64_t ERROR_CLEANUP_SECONDS = 60;
  static constexpr int64_t STOPPED_AFTER_ZLM_DELETE_SECONDS = 30;

  // 2. 计算阶段（无锁）
  for (const auto &[key, metadata] : snapshot) {
    StreamUpdate update;
    update.key = key;
    update.app = metadata.app;
    update.stream = metadata.stream;
    update.old_status = metadata.status;

    // httpflv_gateway特殊处理
    if (metadata.gateway_type == "httpflv_gateway") {
      auto zlm_info = zlm_client_->GetStreamInfo(
          metadata.app, metadata.stream,
          OutputProtocolToSchema(metadata.output_protocol));

      if (!zlm_info.app.empty()) {
        int64_t time_diff = now - metadata.last_update_time;
        if (time_diff > 0 && zlm_info.total_bytes > metadata.total_bytes) {
          update.bytes_speed =
              (zlm_info.total_bytes - metadata.total_bytes) / time_diff;
        } else if (zlm_info.bytes_speed > 0) {
          update.bytes_speed = zlm_info.bytes_speed;
        }
        update.total_bytes = zlm_info.total_bytes;
        update.reader_count = zlm_info.reader_count;
        update.zlm_alive = zlm_info.alive || zlm_info.bytes_speed > 0 ||
                           zlm_info.total_bytes > 0;
      } else {
        int64_t stream_uptime = now - metadata.create_time;
        if (stream_uptime > 0) {
          const int64_t estimated_bitrate_bps = 192000;
          update.bytes_speed = estimated_bitrate_bps;
          if (metadata.total_bytes == 0) {
            update.total_bytes = estimated_bitrate_bps * stream_uptime;
          }
        }
        update.zlm_alive = (metadata.status == StreamStatus::Running);
      }
      updates.push_back(update);
      continue;
    }

    // 查找ZLM流
    std::string protocol_key;
    auto it = zlm_stream_map.end();

    if (!metadata.protocol.empty()) {
      protocol_key = key + "/" + metadata.protocol;
      it = zlm_stream_map.find(protocol_key);
    }

    if (it == zlm_stream_map.end()) {
      for (const auto &schema : {"rtmp", "rtsp", "hls", "ts"}) {
        protocol_key = key + "/" + schema;
        it = zlm_stream_map.find(protocol_key);
        if (it != zlm_stream_map.end())
          break;
      }
    }

    if (it != zlm_stream_map.end()) {
      // ZLM中存在
      const StreamInfo &zlm_info = it->second;
      update.zlm_alive = zlm_info.alive;
      update.reader_count = zlm_info.reader_count;
      update.bytes_speed = zlm_info.bytes_speed;
      update.total_bytes = zlm_info.total_bytes;

      if (zlm_info.stream != metadata.stream) {
        update.zlm_app = zlm_info.app;
        update.zlm_stream = zlm_info.stream;
      }

      bool is_running = ZLMClient::IsStreamActive(zlm_info);

      // Phase 5 Fix (Part 2): Handles case where ZLM returns steam but reports
      // inactive If we seek it as Running and process is alive, trust process.
      if (!is_running && metadata.status == StreamStatus::Running) {
        bool process_alive = false;
        if (metadata.pid > 0) {
          process_alive = (kill(metadata.pid, 0) == 0);
        }
        if (process_alive) {
          is_running = true;
          LOG_DEBUG("PrepareStreamUpdates (ZLM Present): Overriding status to "
                    "Running because process {} is alive",
                    metadata.pid);
        }
      }

      update.new_status =
          is_running ? StreamStatus::Running : StreamStatus::Stopped;

    } else {
      // ZLM中不存在
      update.zlm_alive = false;
      update.reader_count = 0;
      update.bytes_speed = 0;
      update.total_bytes = 0;

      int64_t time_since_update = now - metadata.last_update_time;

      if (metadata.status == StreamStatus::Running) {
        // Phase 5 Check: Only stop if process is effectively dead
        // This handles cases where ZLM API temporarily returns empty (Linux
        // issue) but the FFmpeg process is still running healthy.
        bool process_alive = false;
        if (metadata.pid > 0) {
          // process_monitor.hpp must be included
          process_alive = (kill(metadata.pid, 0) == 0);
        }

        if (!process_alive) {
          update.new_status = StreamStatus::Stopped;
        } else {
          // Process is alive, but ZLM is missing the stream.
          // Check for "Zombie" status: Process running but ZLM inactive for >
          // 30s
          int64_t last_alive_time = metadata.zlm_last_alive_time > 0
                                        ? metadata.zlm_last_alive_time
                                        : metadata.create_time;
          int64_t time_since_alive = now - last_alive_time;

          if (time_since_alive > 30) {
            // Zombie Detected: Mark as Error to trigger recovery
            update.new_status = StreamStatus::Error;
            LOG_WARN("Zombie Stream Detected: {}/{} (PID: {}). Process alive "
                     "but ZLM inactive for {}s. Marking as Error.",
                     metadata.app, metadata.stream, metadata.pid,
                     time_since_alive);
          } else {
            // Within grace period (30s), keep Running
            // This protects against transient ZLM API blips
            LOG_DEBUG(
                "PrepareStreamUpdates: Stream {}/{} ZLM missing but process {} "
                "alive (grace period {}s/30s), keeping Running",
                metadata.app, metadata.stream, metadata.pid, time_since_alive);
          }
        }
      } else if (metadata.status == StreamStatus::Starting) {
        int64_t timeout_seconds = (metadata.pid == 0) ? 60 : 30;
        if (time_since_update > timeout_seconds) {
          update.new_status = StreamStatus::Error;
        }
      } else if (metadata.status == StreamStatus::Error) {
        if (time_since_update > ERROR_CLEANUP_SECONDS) {
          update.should_remove = true;
        }
      } else if (metadata.status == StreamStatus::Stopped) {
        if (time_since_update > STOPPED_AFTER_ZLM_DELETE_SECONDS) {
          update.should_remove = true;
        }
      }
    }

    updates.push_back(update);
  }

  auto total_duration = std::chrono::duration_cast<std::chrono::microseconds>(
                            std::chrono::steady_clock::now() - start_time)
                            .count();
  LOG_INFO("Phase 2.1: PrepareStreamUpdates completed: {} streams, {}μs total "
           "(snapshot: {}μs)",
           updates.size(), total_duration, snapshot_duration);

  return updates;
}

int StreamManager::ApplyStreamUpdates(const std::vector<StreamUpdate> &updates,
                                      int64_t now) {

  auto apply_start = std::chrono::steady_clock::now();
  std::lock_guard<std::mutex> lock(mutex_);
  auto lock_acquired = std::chrono::steady_clock::now();
  int synced_count = 0;

  for (const auto &update : updates) {
    auto it = streams_.find(update.key);
    if (it == streams_.end()) {
      continue; // 流在快照后被删除
    }

    StreamMetadata &metadata = it->second;

    // 应用更新
    if (update.zlm_alive.has_value()) {
      metadata.zlm_alive = *update.zlm_alive;
    }
    if (update.reader_count.has_value()) {
      metadata.reader_count = *update.reader_count;
    }
    if (update.bytes_speed.has_value()) {
      metadata.bytes_speed = *update.bytes_speed;
    }
    if (update.total_bytes.has_value()) {
      metadata.total_bytes = *update.total_bytes;
    }
    if (update.zlm_app.has_value()) {
      metadata.zlm_app = *update.zlm_app;
    }
    if (update.zlm_stream.has_value()) {
      metadata.zlm_stream = *update.zlm_stream;
    }

    // 状态更新
    if (update.new_status.has_value() &&
        *update.new_status != update.old_status) {
      metadata.status = *update.new_status;
      RecordStatusTransition(update.app, update.stream, update.old_status,
                             *update.new_status, update.transition_source);
    }

    metadata.last_update_time = now;
    synced_count++;

    // 删除
    // 删除
    if (update.should_remove) {
      StreamMetadata temp = metadata; // copy
      streams_.erase(it);
      BroadcastStreamUpdate(temp, true, true); // removed=true, force=true
    } else {
      // 只有状态变化或节流时间到才广播
      bool force = (update.new_status.has_value() &&
                    *update.new_status != update.old_status);
      BroadcastStreamUpdate(metadata, false, force);
    }
  }

  auto lock_duration = std::chrono::duration_cast<std::chrono::microseconds>(
                           std::chrono::steady_clock::now() - lock_acquired)
                           .count();
  auto total_apply_duration =
      std::chrono::duration_cast<std::chrono::microseconds>(
          std::chrono::steady_clock::now() - apply_start)
          .count();

  LOG_INFO(
      "Phase 2.1: ApplyStreamUpdates: {} streams, lock held: {}μs, total: {}μs",
      synced_count, lock_duration, total_apply_duration);

  return synced_count;
}

int StreamManager::SyncZLMStatus() {
  if (!zlm_client_) {
    LOG_WARN("ZLM Client 未初始化，无法同步状态");
    return 0;
  }

  // 在锁外收集需要查询的 schema（根据 output_protocol）
  // 优化：尽量只查询我们需要的协议，减少不必要的数据传输
  std::set<std::string> schemas_to_query;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    for (const auto &pair : streams_) {
      const StreamMetadata &metadata = pair.second;
      if (!metadata.output_protocol.empty()) {
        std::string schema = OutputProtocolToSchema(metadata.output_protocol);
        if (!schema.empty()) {
          schemas_to_query.insert(schema);
        }
      }
    }

    // 额外保证：始终查询基础协议 schema（rtmp/rtsp/hls/ts）
    // 场景说明：
    //   - 本地摄像头 Gateway 始终以 RTMP 推流到 ZLM，
    //     但 output_protocol 可能是 http-flv / hls / webrtc
    //   - ZLM 会为同一个 app/stream 生成多个 schema（rtmp, rtsp, hls, ts 等）
    //   - 如果只根据 output_protocol 映射的 schema 去查（例如 http-flv），
    //     而 ZLM 当前还没有该 schema 条目，就会误判为「ZLM 中没有流」
    //   - 为了稳妥起见，这里始终把基础 schema 加入查询列表，
    //     这样可以通过 rtmp/hls/ts 等任意一个 schema 判断 ZLM 是否有该流
    schemas_to_query.insert("rtmp");
    schemas_to_query.insert("rtsp");
    schemas_to_query.insert("hls");
    schemas_to_query.insert("ts");
  }

  // 如果没有指定 output_protocol 的流，或者所有流都是
  // webrtc，则查询所有协议（向后兼容）
  if (schemas_to_query.empty()) {
    schemas_to_query.insert(""); // 空字符串表示查询所有协议
  }

  // 在锁外执行网络请求，减少锁持有时间
  // 批量查询所有需要的 schema
  std::vector<StreamInfo> zlm_streams;
  for (const auto &schema : schemas_to_query) {
    std::vector<StreamInfo> schema_streams = zlm_client_->GetStreamList(schema);
    zlm_streams.insert(zlm_streams.end(), schema_streams.begin(),
                       schema_streams.end());
  }

  // 在锁外准备数据（创建映射表）
  // 注意：同一个 app/stream 可能有多个协议（rtsp, fmp4, hls等），需要按协议匹配
  // key 格式：app/stream/schema
  std::map<std::string, StreamInfo> zlm_stream_map;
  for (const auto &stream : zlm_streams) {
    std::string key =
        GetStreamKey(stream.app, stream.stream) + "/" + stream.schema;
    zlm_stream_map[key] = stream;
  }

  auto now = GetCurrentTimestamp();

  // ============================================
  // Phase 2.1: 批量更新优化 - 新的三阶段流程
  // ============================================

  // 阶段1: 准备更新（无锁计算）
  LOG_DEBUG("Phase 2.1: Preparing stream updates (lock-free)...");
  auto updates = PrepareStreamUpdates(zlm_stream_map, now);
  LOG_DEBUG("Phase 2.1: Prepared {} updates", updates.size());

  // 阶段2: 应用更新（短暂持锁）
  LOG_DEBUG("Phase 2.1: Applying stream updates (brief lock)...");
  int synced_count = ApplyStreamUpdates(updates, now);

  // 阶段3: 处理ZLM新增流的自动注册（需要短暂持锁）
  // 注意：这部分保留原有逻辑，但也优化为批量处理
  int auto_registered = 0;
  {
    std::lock_guard<std::mutex> lock(mutex_);

    for (const auto &pair : zlm_stream_map) {
      const StreamInfo &zlm_info = pair.second;

      // 只自动注册原生协议
      if (zlm_info.schema != "rtsp" && zlm_info.schema != "rtmp") {
        continue;
      }

      std::string stream_key = GetStreamKey(zlm_info.app, zlm_info.stream);

      // 检查手动停止列表
      if (manually_stopped_streams_.count(stream_key) > 0) {
        continue;
      }

      // 检查是否已存在
      if (streams_.count(stream_key) == 0) {
        // 创建新流元数据
        StreamMetadata metadata;
        metadata.app = zlm_info.app;
        metadata.stream = zlm_info.stream;

        // 检查是否是本地摄像头流
        if (!zlm_info.origin_url.empty() &&
            zlm_info.origin_url.find("local-camera://") != std::string::npos) {
          metadata.protocol = "local-camera";
          metadata.gateway_type = "local_camera_gateway";
          metadata.device_type = "local_camera";
        } else {
          // 从origin_url推断协议
          std::string inferred_protocol =
              StreamManager::InferProtocolFromSourceUrl(zlm_info.origin_url);
          if (!inferred_protocol.empty()) {
            metadata.protocol = inferred_protocol;
          } else {
            metadata.protocol = zlm_info.schema;
          }
          metadata.gateway_type = "native";
        }

        metadata.output_protocol = zlm_info.schema;
        metadata.status = ZLMClient::IsStreamActive(zlm_info)
                              ? StreamStatus::Running
                              : StreamStatus::Stopped;
        metadata.create_time = now;
        metadata.last_update_time = now;

        // ZLM统计信息
        metadata.zlm_alive = zlm_info.alive;
        metadata.reader_count = zlm_info.reader_count;
        metadata.bytes_speed = zlm_info.bytes_speed;
        metadata.total_bytes = zlm_info.total_bytes;
        metadata.zlm_app = zlm_info.app;
        metadata.zlm_stream = zlm_info.stream;

        streams_[stream_key] = metadata;
        auto_registered++;

        LOG_INFO("自动注册新流: {} (协议: {}, 来源: {})", stream_key,
                 metadata.protocol,
                 zlm_info.origin_url.empty()
                     ? "unknown"
                     : zlm_info.origin_url.substr(0, 50));

        // 广播新流 (强制)
        BroadcastStreamUpdate(metadata, false, true);
      }
    }
  }

  LOG_DEBUG("同步 ZLMediaKit 状态完成，更新了 {} 个流，自动注册了 {} 个新流",
            synced_count, auto_registered);

  return synced_count + auto_registered;
}

void StreamManager::StartStatusSync(int sync_interval_seconds) {
  if (sync_running_.load()) {
    LOG_WARN("状态同步线程已在运行");
    return;
  }

  sync_running_.store(true);
  sync_thread_ = std::thread(&StreamManager::StatusSyncThread, this,
                             sync_interval_seconds);
  LOG_INFO("启动状态同步线程，同步间隔: {} 秒", sync_interval_seconds);
}

void StreamManager::StopStatusSync() {
  if (!sync_running_.load()) {
    return;
  }

  sync_running_.store(false);

  // 唤醒同步线程
  {
    std::lock_guard<std::mutex> lock(sleep_mutex_);
    sleep_cv_.notify_all();
  }

  if (sync_thread_.joinable()) {
    sync_thread_.join();
  }
  LOG_INFO("停止状态同步线程");
}

void StreamManager::StatusSyncThread(int sync_interval_seconds) {
  while (sync_running_.load()) {
    try {
      SyncZLMStatus();
    } catch (const std::exception &e) {
      LOG_ERROR("状态同步异常: {}", e.what());
    }

    // 等待指定时间间隔 (支持提前唤醒)
    std::unique_lock<std::mutex> lock(sleep_mutex_);
    sleep_cv_.wait_for(lock, std::chrono::seconds(sync_interval_seconds),
                       [this] { return !sync_running_.load(); });
  }
}

size_t StreamManager::GetStreamCount() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return streams_.size();
}

nlohmann::json StreamManager::GetStatusStatistics() const {
  std::lock_guard<std::mutex> lock(mutex_);
  std::lock_guard<std::mutex> stats_lock(stats_mutex_);

  // 实时统计各状态计数
  size_t count_running = 0;
  size_t count_starting = 0;
  size_t count_stopped = 0;
  size_t count_error = 0;
  size_t count_stopping = 0;

  for (const auto &pair : streams_) {
    switch (pair.second.status) {
    case StreamStatus::Running:
      count_running++;
      break;
    case StreamStatus::Starting:
      count_starting++;
      break;
    case StreamStatus::Stopped:
      count_stopped++;
      break;
    case StreamStatus::Error:
      count_error++;
      break;
    case StreamStatus::Stopping:
      count_stopping++;
      break;
    }
  }

  nlohmann::json result;
  result["total_streams"] =
      streams_.size(); // Add top-level total_streams field
  result["status_counts"] = {
      {"running", count_running},   {"starting", count_starting},
      {"stopped", count_stopped},   {"error", count_error},
      {"stopping", count_stopping}, {"total", streams_.size()}};

  result["transition_stats"] = {
      {"gateway_create_requested",
       status_stats_.transitions.gateway_create_requested},
      {"gateway_create_result",
       status_stats_.transitions.gateway_create_result},
      {"zlm_state_update", status_stats_.transitions.zlm_state_update},
      {"sync_polling", status_stats_.transitions.sync_polling},
      {"manual_update", status_stats_.transitions.manual_update}};

  // 最近的状态转换记录（最多 20 条）
  // 复制 vector 以避免在序列化期间被其他线程修改导致崩溃
  // 注意：外层已经持有 stats_lock，所以这里不需要再加锁
  std::vector<StatusStatistics::TransitionDetail> transitions_copy =
      status_stats_.recent_transitions;

  result["recent_transitions"] = nlohmann::json::array();
  size_t start_idx =
      transitions_copy.size() > 20 ? transitions_copy.size() - 20 : 0;
  for (size_t i = start_idx; i < transitions_copy.size(); ++i) {
    const auto &trans = transitions_copy[i];
    result["recent_transitions"].push_back({{"app", trans.app},
                                            {"stream", trans.stream},
                                            {"from", trans.from_status},
                                            {"to", trans.to_status},
                                            {"source", trans.source},
                                            {"timestamp", trans.timestamp}});
  }

  return result;
}

} // namespace streaming
