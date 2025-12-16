#include "process/ffmpeg_executor.hpp"
#include "process/process_manager.hpp"
#include "process/process_monitor.hpp"
#include "gateway/base/gateway_base.hpp"
#include "config/constants.hpp"
#include "utils/logger.hpp"

using namespace config::constants;

#include <unistd.h>
#include <sys/wait.h>
#include <signal.h>
#include <fcntl.h>
#include <errno.h>
#include <cstring>

namespace process {

pid_t FFmpegExecutor::StartProcess(const std::string& command,
                                  std::shared_ptr<ProcessManager> process_manager,
                                  const std::string& process_id,
                                  const std::string& process_name,
                                  const std::string& source_url,
                                  const std::string& target_app,
                                  const std::string& target_stream,
                                  const std::string& log_file) {
    if (process_manager && !process_id.empty()) {
        // 使用 ProcessManager 启动
        if (process_manager->StartProcess(process_id, process_name, command,
                                          source_url, target_app, target_stream)) {
            // 等待进程启动
            std::this_thread::sleep_for(std::chrono::milliseconds(time::PROCESS_STABLE_WAIT_MS));
            auto process_info = process_manager->GetProcessInfo(process_id);
            if (process_info.pid > 0) {
                LOG_DEBUG("[FFmpegExecutor] 通过 ProcessManager 启动进程成功 (PID: {})", process_info.pid);
                return process_info.pid;
            }
        }
        LOG_WARN("[FFmpegExecutor] ProcessManager 启动失败，回退到简单模式");
    }

    // 回退到简单的 fork + exec 模式
    return StartProcessSimple(command, log_file);
}

pid_t FFmpegExecutor::StartProcessSimple(const std::string& command, const std::string& log_file) {
    LOG_DEBUG("[FFmpegExecutor] 启动 FFmpeg 进程（简单模式）: {}", command);

    pid_t pid = fork();
    if (pid < 0) {
        LOG_ERROR("[FFmpegExecutor] fork 失败: {}", strerror(errno));
        return 0;
    } else if (pid == 0) {
        // 子进程：执行 FFmpeg 命令
        // 重定向 stdin 到 /dev/null
        int null_fd = open("/dev/null", O_RDONLY);
        if (null_fd >= 0) {
            dup2(null_fd, STDIN_FILENO);
            close(null_fd);
        }

        // 处理日志输出
        if (!log_file.empty()) {
            int log_fd = open(log_file.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
            if (log_fd >= 0) {
                dup2(log_fd, STDOUT_FILENO);
                dup2(log_fd, STDERR_FILENO);
                close(log_fd);
            }
        } else {
            // 如果没有指定日志文件，重定向到 /dev/null
            int null_fd = open("/dev/null", O_WRONLY);
            if (null_fd >= 0) {
                dup2(null_fd, STDOUT_FILENO);
                dup2(null_fd, STDERR_FILENO);
                close(null_fd);
            }
        }

        // 执行命令
        execl("/bin/sh", "sh", "-c", command.c_str(), nullptr);
        exit(1);  // 如果 exec 失败
    } else {
        // 父进程：返回 PID
        LOG_DEBUG("[FFmpegExecutor] 进程启动成功 (PID: {})", pid);
        return pid;
    }
}

bool FFmpegExecutor::StopProcess(pid_t pid,
                                 std::shared_ptr<ProcessManager> process_manager,
                                 const std::string& process_id,
                                 std::function<void(gateway::GatewayStatus)> on_status_update) {
    if (pid <= 0) {
        return true;  // 没有进程，认为成功
    }

    if (process_manager && !process_id.empty()) {
        // 使用 ProcessManager 停止
        bool success = process_manager->StopProcess(process_id);
        if (on_status_update) {
            on_status_update(success ? gateway::GatewayStatus::Stopped : gateway::GatewayStatus::Error);
        }
        return success;
    }

    // 回退到简单的 kill 模式
    return StopProcessSimple(pid, on_status_update);
}

bool FFmpegExecutor::StopProcessSimple(pid_t pid, std::function<void(gateway::GatewayStatus)> on_status_update) {
    if (pid <= 0) {
        return true;
    }

    if (on_status_update) {
        on_status_update(gateway::GatewayStatus::Stopping);
    }

    // 先检查进程是否还在运行
    if (!ProcessMonitor::IsProcessAlive(pid)) {
        // 进程已经不存在了
        if (on_status_update) {
            on_status_update(gateway::GatewayStatus::Stopped);
        }
        return true;
    }

    // 发送 SIGTERM 信号
    if (kill(pid, SIGTERM) != 0) {
        LOG_WARN("[FFmpegExecutor] 发送 SIGTERM 失败 (PID: {}): {}", pid, strerror(errno));
        // 如果进程不存在，认为停止成功
        if (errno == ESRCH) {
            if (on_status_update) {
                on_status_update(gateway::GatewayStatus::Stopped);
            }
            return true;
        }
        if (on_status_update) {
            on_status_update(gateway::GatewayStatus::Error);
        }
        return false;
    }

    // 等待进程退出（最多等待 5 秒）
    for (int i = 0; i < 50; ++i) {
        if (!ProcessMonitor::IsProcessAlive(pid)) {
            if (on_status_update) {
                on_status_update(gateway::GatewayStatus::Stopped);
            }
            return true;
        }
        usleep(100000);  // 100ms
    }

    // 如果进程还在运行，发送 SIGKILL
    LOG_WARN("[FFmpegExecutor] 进程未响应 SIGTERM，发送 SIGKILL (PID: {})", pid);
    if (kill(pid, SIGKILL) == 0) {
        usleep(100000);  // 等待 100ms
        if (!ProcessMonitor::IsProcessAlive(pid)) {
            if (on_status_update) {
                on_status_update(gateway::GatewayStatus::Stopped);
            }
            return true;
        }
    } else {
        // SIGKILL 发送失败，检查进程是否已经退出
        if (errno == ESRCH || !ProcessMonitor::IsProcessAlive(pid)) {
            if (on_status_update) {
                on_status_update(gateway::GatewayStatus::Stopped);
            }
            return true;
        }
    }

    // 最终检查：如果进程确实不存在了，认为停止成功
    if (!ProcessMonitor::IsProcessAlive(pid)) {
        if (on_status_update) {
            on_status_update(gateway::GatewayStatus::Stopped);
        }
        return true;
    }

    if (on_status_update) {
        on_status_update(gateway::GatewayStatus::Error);
    }
    return false;
}

} // namespace process

