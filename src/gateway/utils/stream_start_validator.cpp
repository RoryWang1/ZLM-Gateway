#include "gateway/utils/stream_start_validator.hpp"

#include "process/process_manager.hpp"
#include "process/process_monitor.hpp"
#include "streaming/zlmediakit/zlm_client.hpp"
#include "utils/logger.hpp"
#include "utils/zlm_stream_checker.hpp"

#include <algorithm>
#include <chrono>
#include <thread>

namespace gateway {
namespace utils {

StreamStartValidator::StreamStartValidator(
    std::shared_ptr<streaming::ZLMClient> zlm_client,
    std::shared_ptr<process::ProcessManager> process_manager,
    const StreamStartValidationConfig &config)
    : zlm_client_(std::move(zlm_client)),
      process_manager_(std::move(process_manager)), config_(config) {}

bool StreamStartValidator::ValidateWithProcessManager(
    int &pid, GatewayStatus &status, const std::string &process_id,
    const std::string &target_app, const std::string &target_stream,
    std::function<bool()> start_process_callback,
    std::function<std::string()> read_error_log_callback) {
  if (!process_manager_) {
    LOG_ERROR("[StreamStartValidator] ProcessManager is null when "
              "ValidateWithProcessManager is called");
    status = GatewayStatus::Error;
    pid = 0;
    return false;
  }

  if (!start_process_callback || !start_process_callback()) {
    LOG_ERROR(
        "[StreamStartValidator] ProcessManager::StartProcess returned false");
    status = GatewayStatus::Error;
    pid = 0;
    return false;
  }

  // 等待进程启动稳定
  std::this_thread::sleep_for(
      std::chrono::milliseconds(config_.process_stable_wait_ms));

  auto process_info = process_manager_->GetProcessInfo(process_id);
  if (process_info.status == process::ProcessStatus::Error ||
      !process::ProcessMonitor::IsProcessAlive(process_info.pid)) {
    LOG_ERROR("[StreamStartValidator] FFmpeg 进程启动后失败: {}/{} (PID: {})",
              target_app, target_stream, process_info.pid);
    LogError(target_app, target_stream, read_error_log_callback);
    status = GatewayStatus::Error;
    pid = 0;
    return false;
  }

  pid = process_info.pid;
  status = (process_info.status == process::ProcessStatus::Running)
               ? GatewayStatus::Running
               : GatewayStatus::Starting;

  // 等待 ZLM 中流就绪
  return WaitForStreamReadyWithProcessManager(pid, status, process_id,
                                              target_app, target_stream,
                                              read_error_log_callback);
}

bool StreamStartValidator::ValidateSimple(
    int &pid, GatewayStatus &status, const std::string &target_app,
    const std::string &target_stream,
    std::function<pid_t()> start_process_callback,
    std::function<std::string()> read_error_log_callback,
    std::function<streaming::StreamInfo(
        const std::string &, const std::string &, const std::string &)>
        checker) {
  if (!start_process_callback) {
    LOG_ERROR("[StreamStartValidator] start_process_callback is null");
    status = GatewayStatus::Error;
    pid = 0;
    return false;
  }

  pid_t started_pid = start_process_callback();
  if (started_pid <= 0) {
    LOG_ERROR("[StreamStartValidator] StartFFmpegProcess returned false");
    status = GatewayStatus::Error;
    pid = 0;
    return false;
  }

  pid = static_cast<int>(started_pid);
  status = GatewayStatus::Starting;

  // 等待进程稳定
  std::this_thread::sleep_for(
      std::chrono::milliseconds(config_.process_stable_wait_ms));

  if (!process::ProcessMonitor::IsProcessAlive(pid)) {
    LOG_ERROR("[StreamStartValidator] FFmpeg 进程启动后失败: {}/{} (PID: {})",
              target_app, target_stream, pid);
    LogError(target_app, target_stream, read_error_log_callback);
    status = GatewayStatus::Error;
    pid = 0;
    return false;
  }

  // 等待 ZLM 中流就绪
  return WaitForStreamReadySimple(pid, status, target_app, target_stream,
                                  read_error_log_callback, checker);
}

bool StreamStartValidator::WaitForStreamReadyWithProcessManager(
    int &pid, GatewayStatus &status, const std::string &process_id,
    const std::string &target_app, const std::string &target_stream,
    std::function<std::string()> read_error_log_callback) {
  if (!process_manager_) {
    LOG_ERROR("[StreamStartValidator] ProcessManager is null in "
              "WaitForStreamReadyWithProcessManager");
    status = GatewayStatus::Error;
    pid = 0;
    return false;
  }

  int total_wait_time = 0;
  int check_interval = config_.zlm_check_interval_ms;
  int remaining_timeout = config_.zlm_check_timeout_ms;

  for (int attempt = 0; attempt < config_.max_check_attempts &&
                        total_wait_time < config_.zlm_check_timeout_ms;
       ++attempt) {
    std::this_thread::sleep_for(std::chrono::milliseconds(check_interval));
    total_wait_time += check_interval;

    // 检查进程是否存活
    auto process_info = process_manager_->GetProcessInfo(process_id);
    if (!process::ProcessMonitor::IsProcessAlive(process_info.pid)) {
      LOG_ERROR("[StreamStartValidator] FFmpeg 进程已退出，流启动失败: {}/{} "
                "(等待时间: {}ms)",
                target_app, target_stream, total_wait_time);
      LogError(target_app, target_stream, read_error_log_callback);
      status = GatewayStatus::Error;
      pid = 0;
      return false;
    }

    // 检查 ZLM 中是否有流
    if (CheckZLMStream(target_app, target_stream)) {
      status = GatewayStatus::Running;
      LOG_INFO(
          "[StreamStartValidator] 流已成功推送到 ZLM: {}/{} (等待时间: {}ms)",
          target_app, target_stream, total_wait_time);
      return true;
    }

    // 动态调整检查间隔
    if (attempt == 0 && config_.max_check_attempts > 1) {
      remaining_timeout -= check_interval;
      check_interval =
          std::min(remaining_timeout, config_.zlm_check_interval_ms * 2);
    }
  }

  // 超时后仍没有流
  auto updated_process_info = process_manager_->GetProcessInfo(process_id);
  if (!process::ProcessMonitor::IsProcessAlive(updated_process_info.pid)) {
    LOG_ERROR("[StreamStartValidator] FFmpeg 进程已退出，流启动失败: {}/{} "
              "(等待时间: {}ms)",
              target_app, target_stream, total_wait_time);
    LogError(target_app, target_stream, read_error_log_callback);
    status = GatewayStatus::Error;
    pid = 0;
    return false;
  } else {
    // 进程还在运行，但在超时时间内没有在 ZLM 中检测到流
    // 检查 ZLM 连接是否正常
    if (!zlm_client_) {
      LOG_ERROR("[StreamStartValidator] ZLM 客户端未初始化，流验证失败: {}/{}",
                target_app, target_stream);
      status = GatewayStatus::Error;
      return false;
    }

    // 获取详细的流信息用于诊断
    try {
      auto stream_info = zlm_client_->GetStreamInfo(target_app, target_stream);
      if (!stream_info.app.empty()) {
        // 流存在但未活跃
        LOG_WARN("[StreamStartValidator] FFmpeg 进程运行中，但 ZLM 中流未活跃: "
                 "{}/{} (等待时间: {}ms, alive={}, bytesSpeed={}, "
                 "totalBytes={})，流验证失败",
                 target_app, target_stream, total_wait_time, stream_info.alive,
                 stream_info.bytes_speed, stream_info.total_bytes);
      } else {
        // 流不存在
        LOG_WARN("[StreamStartValidator] FFmpeg 进程运行中，但 ZLM 中 {}ms "
                 "后仍没有流: {}/{}，流验证失败",
                 total_wait_time, target_app, target_stream);
      }
      status = GatewayStatus::Error;
      // 保留 pid，不置 0，让上层可以检查进程状态
      return false;
    } catch (const std::exception &e) {
      LOG_ERROR("[StreamStartValidator] ZLM 连接异常: {}，流验证失败: {}/{}",
                e.what(), target_app, target_stream);
      status = GatewayStatus::Error;
      return false;
    }
  }
}

bool StreamStartValidator::WaitForStreamReadySimple(
    int pid, GatewayStatus &status, const std::string &target_app,
    const std::string &target_stream,
    std::function<std::string()> read_error_log_callback,
    std::function<streaming::StreamInfo(
        const std::string &, const std::string &, const std::string &)>
        checker) {
  int total_wait_time = 0;
  int check_interval = config_.zlm_check_interval_ms;
  int remaining_timeout = config_.zlm_check_timeout_ms;

  for (int attempt = 0; attempt < config_.max_check_attempts &&
                        total_wait_time < config_.zlm_check_timeout_ms;
       ++attempt) {
    std::this_thread::sleep_for(std::chrono::milliseconds(check_interval));
    total_wait_time += check_interval;

    // 检查进程是否存活
    if (!process::ProcessMonitor::IsProcessAlive(pid)) {
      LOG_ERROR("[StreamStartValidator] FFmpeg 进程已退出，流启动失败: {}/{} "
                "(等待时间: {}ms)",
                target_app, target_stream, total_wait_time);
      LogError(target_app, target_stream, read_error_log_callback);
      status = GatewayStatus::Error;
      return false;
    }

    // 检查 ZLM 中是否有流
    bool stream_ready = false;
    if (checker) {
      // 使用提供的 checker (通常带 schema 修正逻辑)
      auto info = checker(target_app, target_stream, "");
      // 如果返回了 info 且 app 非空，说明存在
      if (!info.app.empty()) {
        stream_ready = true;
      }
    } else {
      stream_ready = CheckZLMStream(target_app, target_stream);
    }

    if (stream_ready) {
      status = GatewayStatus::Running;
      LOG_INFO(
          "[StreamStartValidator] 流已成功推送到 ZLM: {}/{} (等待时间: {}ms)",
          target_app, target_stream, total_wait_time);
      return true;
    }

    // 动态调整检查间隔
    if (attempt == 0 && config_.max_check_attempts > 1) {
      remaining_timeout -= check_interval;
      check_interval =
          std::min(remaining_timeout, config_.zlm_check_interval_ms * 2);
    }
  }

  // 超时后仍没有流
  if (!process::ProcessMonitor::IsProcessAlive(pid)) {
    LOG_ERROR("[StreamStartValidator] FFmpeg 进程已退出，流启动失败: {}/{} "
              "(等待时间: {}ms)",
              target_app, target_stream, total_wait_time);
    LogError(target_app, target_stream, read_error_log_callback);
    status = GatewayStatus::Error;
    return false;
  } else {
    // 进程还在运行，但在超时时间内没有在 ZLM 中检测到流
    // 检查 ZLM 连接是否正常
    if (!zlm_client_) {
      LOG_ERROR("[StreamStartValidator] ZLM 客户端未初始化，流验证失败: {}/{}",
                target_app, target_stream);
      status = GatewayStatus::Error;
      return false;
    }

    // 获取详细的流信息用于诊断
    LogError(target_app, target_stream, read_error_log_callback);

    streaming::StreamInfo stream_info;
    if (checker) {
      stream_info = checker(target_app, target_stream, "");
    } else if (zlm_client_) {
      stream_info = zlm_client_->GetStreamInfo(target_app, target_stream);
    }

    if (!stream_info.app.empty()) {
      // 流存在但未活跃
      LOG_WARN("[StreamStartValidator] FFmpeg 进程运行中，但 ZLM 中流未活跃: "
               "{}/{} (等待时间: {}ms, alive={}, bytesSpeed={}, "
               "totalBytes={})，流验证失败",
               target_app, target_stream, total_wait_time, stream_info.alive,
               stream_info.bytes_speed, stream_info.total_bytes);
    } else {
      // 流不存在
      LOG_WARN("[StreamStartValidator] FFmpeg 进程运行中，但 ZLM 中 {}ms "
               "后仍没有流: {}/{}，流验证失败",
               total_wait_time, target_app, target_stream);
    }
    status = GatewayStatus::Error;
    return false;
  }
}

bool StreamStartValidator::CheckZLMStream(
    const std::string &target_app, const std::string &target_stream) const {
  return ::utils::ZLMStreamChecker::IsStreamAlive(zlm_client_, target_app,
                                                  target_stream);
}

bool StreamStartValidator::CheckZLMStreamActive(
    const std::string &target_app, const std::string &target_stream) const {
  if (!zlm_client_) {
    return false;
  }
  auto stream_info = zlm_client_->GetStreamInfo(target_app, target_stream);
  return streaming::ZLMClient::IsStreamActive(stream_info);
}

bool StreamStartValidator::ValidateNative(
    GatewayStatus &status, const std::string &target_app,
    const std::string &target_stream, const std::string &source_url,
    std::function<bool(const std::string &, const std::string &,
                       const std::string &)>
        start_native_callback,
    bool require_active) {
  if (!zlm_client_) {
    LOG_ERROR("[StreamStartValidator] ZLMClient is null when ValidateNative is "
              "called");
    status = GatewayStatus::Error;
    return false;
  }

  if (!start_native_callback) {
    LOG_ERROR("[StreamStartValidator] start_native_callback is null");
    status = GatewayStatus::Error;
    return false;
  }

  // 调用 ZLM API 添加流
  if (!start_native_callback(target_app, target_stream, source_url)) {
    LOG_ERROR("[StreamStartValidator] 调用 ZLM API 添加流失败: {}/{}",
              target_app, target_stream);
    status = GatewayStatus::Error;
    return false;
  }

  status = GatewayStatus::Starting;

  // 等待流在 ZLM 中建立并活跃
  int total_wait_time = 0;
  int check_interval = config_.zlm_check_interval_ms;
  int remaining_timeout = config_.zlm_check_timeout_ms;

  for (int attempt = 0; attempt < config_.max_check_attempts &&
                        total_wait_time < config_.zlm_check_timeout_ms;
       ++attempt) {
    std::this_thread::sleep_for(std::chrono::milliseconds(check_interval));
    total_wait_time += check_interval;

    // 检查流是否活跃（有数据传输）
    if (require_active) {
      if (CheckZLMStreamActive(target_app, target_stream)) {
        status = GatewayStatus::Running;
        LOG_INFO("[StreamStartValidator] 原生流已成功建立并活跃: {}/{} "
                 "(等待时间: {}ms)",
                 target_app, target_stream, total_wait_time);
        return true;
      }
    } else {
      // 如果不要求活跃，只检查流是否存在
      if (CheckZLMStream(target_app, target_stream)) {
        status = GatewayStatus::Running;
        LOG_INFO(
            "[StreamStartValidator] 原生流已成功建立: {}/{} (等待时间: {}ms)",
            target_app, target_stream, total_wait_time);
        return true;
      }
    }

    // 动态调整检查间隔
    if (attempt == 0 && config_.max_check_attempts > 1) {
      remaining_timeout -= check_interval;
      check_interval =
          std::min(remaining_timeout, config_.zlm_check_interval_ms * 2);
    }
  }

  // 超时后仍没有流或流未活跃
  auto final_stream_info =
      zlm_client_->GetStreamInfo(target_app, target_stream);
  if (require_active) {
    if (streaming::ZLMClient::IsStreamActive(final_stream_info)) {
      // 最后一次检查成功
      status = GatewayStatus::Running;
      LOG_INFO("[StreamStartValidator] 原生流已成功建立并活跃（超时后）: {}/{} "
               "(等待时间: {}ms)",
               target_app, target_stream, total_wait_time);
      return true;
    } else if (!final_stream_info.app.empty()) {
      // 流记录存在但未活跃，可能是源流不存在或需要更多时间
      // 如果 require_active=true，说明需要流活跃，但流未活跃，应该返回 false
      // 但如果流记录存在，说明 AddStreamProxy
      // 成功了，只是源流可能不存在或需要更多时间 设置为 Starting
      // 状态，让后续的状态检查来更新
      status = GatewayStatus::Starting;
      LOG_WARN("[StreamStartValidator] "
               "原生流已创建但未活跃（{}ms内无数据传输），设置为 Starting "
               "状态: {}/{} (alive={}, bytesSpeed={}, totalBytes={})",
               total_wait_time, target_app, target_stream,
               final_stream_info.alive, final_stream_info.bytes_speed,
               final_stream_info.total_bytes);
      // 注意：即使流未活跃，也返回 true，因为 AddStreamProxy/AddRTMPStream
      // 已成功 后续的状态同步会检查流是否真正活跃，如果长时间不活跃会更新为
      // Error
      return true;
    } else {
      // 流记录不存在，AddStreamProxy/AddRTMPStream 可能失败了
      LOG_ERROR("[StreamStartValidator] 原生流创建失败，流记录不存在: {}/{} "
                "(等待时间: {}ms)",
                target_app, target_stream, total_wait_time);
      status = GatewayStatus::Error;
      return false;
    }
  } else {
    // 不要求活跃，只检查流是否存在
    if (CheckZLMStream(target_app, target_stream)) {
      status = GatewayStatus::Running;
      LOG_INFO("[StreamStartValidator] 原生流已成功建立（超时后）: {}/{} "
               "(等待时间: {}ms)",
               target_app, target_stream, total_wait_time);
      return true;
    } else {
      LOG_ERROR("[StreamStartValidator] 原生流创建失败，流记录不存在: {}/{} "
                "(等待时间: {}ms)",
                target_app, target_stream, total_wait_time);
      status = GatewayStatus::Error;
      return false;
    }
  }
}

void StreamStartValidator::LogError(
    const std::string & /*target_app*/, const std::string & /*target_stream*/,
    std::function<std::string()> read_error_log_callback) const {
  if (!read_error_log_callback) {
    return;
  }

  std::string error_log = read_error_log_callback();
  if (!error_log.empty()) {
    LOG_ERROR("[StreamStartValidator] FFmpeg 错误日志 (最后500字符):\n{}",
              error_log);
  }
}

} // namespace utils
} // namespace gateway
