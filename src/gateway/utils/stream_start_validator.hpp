#ifndef GATEWAY_UTILS_STREAM_START_VALIDATOR_HPP
#define GATEWAY_UTILS_STREAM_START_VALIDATOR_HPP

#include "gateway/base/gateway_base.hpp"
#include <memory>
#include <functional>
#include <string>

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
namespace utils {

/**
 * @brief 流启动验证配置
 *
 * 主要用于控制「等待 + 进程检查 + ZLM 检查」的时序参数。
 */
struct StreamStartValidationConfig {
    int process_stable_wait_ms = 500;   // 进程稳定等待时间（毫秒）
    int zlm_check_interval_ms = 500;    // ZLM 检查间隔（毫秒）
    int zlm_check_timeout_ms = 10000;   // ZLM 检查超时（毫秒）
    int max_check_attempts = 20;        // 最大检查次数
};

/**
 * @brief 通用的流启动验证器
 *
 * 把 LocalCamera 中 ValidateSimple / WaitForStreamReady 里的
 * 「等待 + 进程检查 + ZLM 检查 + 读错误日志」流程抽象出来，
 * 供各个 Gateway 复用。
 *
 * 该类不依赖具体的 StreamInfo 结构，只通过 pid / status 的引用进行更新。
 */
class StreamStartValidator {
public:
    StreamStartValidator(std::shared_ptr<streaming::ZLMClient> zlm_client,
                         std::shared_ptr<process::ProcessManager> process_manager,
                         const StreamStartValidationConfig& config);

    /**
     * @brief 使用 ProcessManager 启动并验证流
     *
     * @param pid                     [in/out] 进程 PID
     * @param status                  [in/out] 网关状态
     * @param process_id              ProcessManager 中的进程 ID
     * @param target_app              目标应用名
     * @param target_stream           目标流名
     * @param start_process_callback  启动进程的回调（返回是否成功）
     * @param read_error_log_callback 读取错误日志的回调（可选）
     * @return 是否成功
     */
    bool ValidateWithProcessManager(
        int& pid,
        GatewayStatus& status,
        const std::string& process_id,
        const std::string& target_app,
        const std::string& target_stream,
        std::function<bool()> start_process_callback,
        std::function<std::string()> read_error_log_callback = nullptr);

    /**
     * @brief 简单模式启动并验证流（不使用 ProcessManager）
     *
     * @param pid                     [in/out] 进程 PID
     * @param status                  [in/out] 网关状态
     * @param target_app              目标应用名
     * @param target_stream           目标流名
     * @param start_process_callback  启动进程的回调（返回 PID，失败返回 0）
     * @param read_error_log_callback 读取错误日志的回调（可选）
     * @return 是否成功
     */
    bool ValidateSimple(
        int& pid,
        GatewayStatus& status,
        const std::string& target_app,
        const std::string& target_stream,
        std::function<pid_t()> start_process_callback,
        std::function<std::string()> read_error_log_callback = nullptr);

    /**
     * @brief 原生协议模式启动并验证流（纯 ZLM API，不使用 FFmpeg）
     *
     * 用于 RTMP、HLS、HTTPFLV 直接代理等原生协议网关。
     * 验证流程：调用 ZLM API -> 等待流建立 -> 检查流是否活跃（有数据传输）
     *
     * @param status                  [in/out] 网关状态
     * @param target_app              目标应用名
     * @param target_stream           目标流名
     * @param source_url              源流 URL
     * @param start_native_callback   启动原生流的回调（调用 ZLM API，返回是否成功）
     * @param require_active          是否要求流必须活跃（有数据传输），默认 true
     * @return 是否成功
     */
    bool ValidateNative(
        GatewayStatus& status,
        const std::string& target_app,
        const std::string& target_stream,
        const std::string& source_url,
        std::function<bool(const std::string&, const std::string&, const std::string&)> start_native_callback,
        bool require_active = true);

private:
    bool WaitForStreamReadyWithProcessManager(
        int& pid,
        GatewayStatus& status,
        const std::string& process_id,
        const std::string& target_app,
        const std::string& target_stream,
        std::function<std::string()> read_error_log_callback);

    bool WaitForStreamReadySimple(
        int pid,
        GatewayStatus& status,
        const std::string& target_app,
        const std::string& target_stream,
        std::function<std::string()> read_error_log_callback);

    bool CheckZLMStream(const std::string& target_app,
                        const std::string& target_stream) const;

    bool CheckZLMStreamActive(const std::string& target_app,
                              const std::string& target_stream) const;

    void LogError(const std::string& target_app,
                  const std::string& target_stream,
                  std::function<std::string()> read_error_log_callback) const;

private:
    std::shared_ptr<streaming::ZLMClient> zlm_client_;
    std::shared_ptr<process::ProcessManager> process_manager_;
    StreamStartValidationConfig config_;
};

} // namespace utils
} // namespace gateway

#endif // GATEWAY_UTILS_STREAM_START_VALIDATOR_HPP


