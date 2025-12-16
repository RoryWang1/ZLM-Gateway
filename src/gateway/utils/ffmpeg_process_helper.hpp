#ifndef GATEWAY_UTILS_FFMPEG_PROCESS_HELPER_HPP
#define GATEWAY_UTILS_FFMPEG_PROCESS_HELPER_HPP

#include "gateway/base/gateway_base.hpp"
#include "process/process_manager.hpp"
#include <string>
#include <memory>
#include <functional>

namespace process {
class ProcessManager;
}

namespace gateway {
namespace utils {

/**
 * @brief FFmpeg 进程管理辅助类
 * 
 * 封装所有 Gateway 中重复的 FFmpeg 进程管理逻辑：
 * - 启动 FFmpeg 进程
 * - 停止 FFmpeg 进程
 * - 读取错误日志
 */
class FFmpegProcessHelper {
public:
    /**
     * @brief 构造函数
     * @param process_manager 进程管理器（可选）
     */
    explicit FFmpegProcessHelper(std::shared_ptr<process::ProcessManager> process_manager = nullptr);

    /**
     * @brief 启动 FFmpeg 进程
     * 
     * @param command FFmpeg 命令
     * @param process_id 进程ID（用于 ProcessManager）
     * @param log_file 日志文件路径（可选）
     * @param pid 输出：进程PID
     * @param status 输出：状态
     * @return 是否成功
     */
    bool StartProcess(const std::string& command,
                     const std::string& process_id,
                     const std::string& log_file,
                     int& pid,
                     GatewayStatus& status);

    /**
     * @brief 停止 FFmpeg 进程
     * 
     * @param pid 进程PID
     * @param process_id 进程ID（用于 ProcessManager）
     * @param status_callback 状态更新回调（可选）
     * @return 是否成功
     */
    bool StopProcess(int pid,
                    const std::string& process_id,
                    std::function<void(GatewayStatus)> status_callback = nullptr);

    /**
     * @brief 读取 FFmpeg 错误日志
     * 
     * @param process_id 进程ID
     * @param log_file 日志文件路径（可选，如果提供则优先使用）
     * @return 错误日志内容（最后500字符）
     */
    std::string ReadErrorLog(const std::string& process_id,
                            const std::string& log_file = "");

    /**
     * @brief 检查进程是否运行中
     * 
     * @param pid 进程PID
     * @return 是否运行中
     */
    static bool IsProcessRunning(int pid);

private:
    std::shared_ptr<process::ProcessManager> process_manager_;
};

} // namespace utils
} // namespace gateway

#endif // GATEWAY_UTILS_FFMPEG_PROCESS_HELPER_HPP

