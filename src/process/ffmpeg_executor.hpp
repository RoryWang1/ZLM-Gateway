#ifndef PROCESS_FFMPEG_EXECUTOR_HPP
#define PROCESS_FFMPEG_EXECUTOR_HPP

#include <string>
#include <memory>
#include <functional>

namespace process {
class ProcessManager;
}

namespace gateway {
enum class GatewayStatus;
}

/**
 * @brief FFmpeg 进程执行器
 * 
 * 提供统一的 FFmpeg 进程启动和停止接口，消除各 gateway 中的重复代码
 */
namespace process {

class FFmpegExecutor {
public:
    /**
     * @brief 启动 FFmpeg 进程
     * @param command FFmpeg 命令字符串
     * @param process_manager 进程管理器（可选，如果提供则使用统一的进程管理）
     * @param process_id 进程ID（用于 ProcessManager）
     * @param process_name 进程名称（用于 ProcessManager）
     * @param source_url 源URL（用于 ProcessManager）
     * @param target_app 目标应用名（用于 ProcessManager）
     * @param target_stream 目标流名（用于 ProcessManager）
     * @param log_file 日志文件路径（可选，用于回退模式）
     * @return 进程PID，失败返回0
     */
    static pid_t StartProcess(const std::string& command,
                              std::shared_ptr<ProcessManager> process_manager = nullptr,
                              const std::string& process_id = "",
                              const std::string& process_name = "",
                              const std::string& source_url = "",
                              const std::string& target_app = "",
                              const std::string& target_stream = "",
                              const std::string& log_file = "");

    /**
     * @brief 停止 FFmpeg 进程
     * @param pid 进程PID
     * @param process_manager 进程管理器（可选）
     * @param process_id 进程ID（用于 ProcessManager）
     * @param on_status_update 状态更新回调函数（可选）
     * @return 是否成功
     */
    static bool StopProcess(pid_t pid,
                           std::shared_ptr<ProcessManager> process_manager = nullptr,
                           const std::string& process_id = "",
                           std::function<void(gateway::GatewayStatus)> on_status_update = nullptr);

private:
    /**
     * @brief 使用 fork + exec 启动进程（回退模式）
     * @param command 命令字符串
     * @param log_file 日志文件路径（可选）
     * @return 进程PID，失败返回0
     */
    static pid_t StartProcessSimple(const std::string& command, const std::string& log_file = "");

    /**
     * @brief 使用 kill 信号停止进程（回退模式）
     * @param pid 进程PID
     * @param on_status_update 状态更新回调函数（可选）
     * @return 是否成功
     */
    static bool StopProcessSimple(pid_t pid, std::function<void(gateway::GatewayStatus)> on_status_update = nullptr);
};

} // namespace process

#endif // PROCESS_FFMPEG_EXECUTOR_HPP

