#include "gateway/local_camera/stream_validator.hpp"
#include "gateway/local_camera/local_camera_gateway.hpp"  // 需要 LocalCameraStreamInfo 的完整定义
#include "config/config_loader.hpp"
#include "streaming/zlmediakit/zlm_client.hpp"
#include "process/process_manager.hpp"

namespace gateway {
namespace local_camera {

StreamValidator::StreamValidator(std::shared_ptr<config::Config> config,
                                std::shared_ptr<streaming::ZLMClient> zlm_client,
                                std::shared_ptr<process::ProcessManager> process_manager)
    : config_(config),
      zlm_client_(zlm_client),
      process_manager_(process_manager),
      validation_config_() {  // 使用默认值初始化
    // 先使用通用默认值
    validation_config_.process_stable_wait_ms = 500;
    validation_config_.zlm_check_interval_ms = 500;
    validation_config_.zlm_check_timeout_ms = 10000;
    validation_config_.max_check_attempts = 20;

    // 再尝试从配置中覆盖
    if (config_) {
        const auto& cfg = config_->local_camera.stream_start;
        validation_config_.process_stable_wait_ms = cfg.process_stable_wait_ms;
        validation_config_.zlm_check_interval_ms = cfg.zlm_check_interval_ms;
        validation_config_.zlm_check_timeout_ms = cfg.zlm_check_timeout_ms;
        validation_config_.max_check_attempts = cfg.max_check_attempts;
    }
}

bool StreamValidator::ValidateWithProcessManager(::gateway::LocalCameraStreamInfo& info,
                                                 const std::string& process_id,
                                                 const std::string& target_app,
                                                 const std::string& target_stream,
                                                 std::function<bool()> start_process_callback) {
    if (!zlm_client_ || !process_manager_) {
        return false;
    }

    // 每次调用时构造一个临时的通用验证器，避免在构造函数阶段复杂初始化导致问题
    utils::StreamStartValidator validator(zlm_client_, process_manager_, validation_config_);

    return validator.ValidateWithProcessManager(
        info.pid,
        info.status,
        process_id,
        target_app,
        target_stream,
        std::move(start_process_callback));
}

bool StreamValidator::ValidateSimple(::gateway::LocalCameraStreamInfo& info,
                                     const std::string& target_app,
                                     const std::string& target_stream,
                                     std::function<pid_t()> start_process_callback,
                                     std::function<std::string()> read_error_log_callback) {
    // 简单模式下不依赖 ProcessManager，只要 ZLMClient 存在即可
    if (!zlm_client_) {
        return false;
    }

    utils::StreamStartValidator validator(zlm_client_, process_manager_, validation_config_);

    return validator.ValidateSimple(
        info.pid,
        info.status,
        target_app,
        target_stream,
        std::move(start_process_callback),
        std::move(read_error_log_callback));
}

} // namespace local_camera
} // namespace gateway

