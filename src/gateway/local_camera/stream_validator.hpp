#ifndef GATEWAY_LOCAL_CAMERA_STREAM_VALIDATOR_HPP
#define GATEWAY_LOCAL_CAMERA_STREAM_VALIDATOR_HPP

#include "gateway/base/gateway_base.hpp"
#include "gateway/utils/stream_start_validator.hpp"
#include <string>
#include <memory>
#include <functional>

namespace config {
struct Config;
}

namespace streaming {
class ZLMClient;
}

namespace process {
class ProcessManager;
}

namespace gateway {
// 前向声明 LocalCameraStreamInfo（在 gateway 命名空间中定义）
struct LocalCameraStreamInfo;

namespace local_camera {

/**
 * @brief LocalCamera 专用的流启动验证器包装
 * 
 * 内部复用通用的 gateway::utils::StreamStartValidator，
 * 负责把 LocalCameraStreamInfo 映射到通用验证器所需的 pid / status。
 */
class StreamValidator {
public:
    /**
     * @brief 构造函数
     * @param config 配置对象
     * @param zlm_client ZLMediaKit 客户端
     * @param process_manager 进程管理器（可选）
     */
    StreamValidator(std::shared_ptr<config::Config> config,
                   std::shared_ptr<streaming::ZLMClient> zlm_client,
                   std::shared_ptr<process::ProcessManager> process_manager = nullptr);

    /**
     * @brief 验证流启动（使用 ProcessManager）
     * @param info 流信息（会被更新）
     * @param process_id 进程ID
     * @param target_app 目标应用名
     * @param target_stream 目标流名
     * @param start_process_callback 启动进程的回调函数（返回是否成功）
     * @return 是否成功
     */
    bool ValidateWithProcessManager(gateway::LocalCameraStreamInfo& info,
                                   const std::string& process_id,
                                   const std::string& target_app,
                                   const std::string& target_stream,
                                   std::function<bool()> start_process_callback);

    /**
     * @brief 验证流启动（简单模式，不使用 ProcessManager）
     * @param info 流信息（会被更新）
     * @param target_app 目标应用名
     * @param target_stream 目标流名
     * @param start_process_callback 启动进程的回调函数（返回 PID，失败返回 0）
     * @param read_error_log_callback 读取错误日志的回调函数（可选）
     * @return 是否成功
     */
    bool ValidateSimple(::gateway::LocalCameraStreamInfo& info,
                       const std::string& target_app,
                       const std::string& target_stream,
                       std::function<pid_t()> start_process_callback,
                           std::function<std::string()> read_error_log_callback = nullptr);

private:
    std::shared_ptr<config::Config> config_;
    std::shared_ptr<streaming::ZLMClient> zlm_client_;
    std::shared_ptr<process::ProcessManager> process_manager_;
    utils::StreamStartValidationConfig validation_config_;
};

} // namespace local_camera
} // namespace gateway

#endif // GATEWAY_LOCAL_CAMERA_STREAM_VALIDATOR_HPP

