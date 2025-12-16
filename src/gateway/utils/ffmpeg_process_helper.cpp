#include "gateway/utils/ffmpeg_process_helper.hpp"
#include "process/ffmpeg_executor.hpp"
#include "process/process_monitor.hpp"
#include "utils/logger.hpp"
#include <fstream>
#include <sstream>
#include <thread>
#include <chrono>

namespace gateway {
namespace utils {

FFmpegProcessHelper::FFmpegProcessHelper(std::shared_ptr<process::ProcessManager> process_manager)
    : process_manager_(process_manager) {
}

bool FFmpegProcessHelper::StartProcess(const std::string& command,
                                      const std::string& process_id,
                                      const std::string& log_file,
                                      int& pid,
                                      GatewayStatus& status) {
    // DEBUG: Log the full FFmpeg command before spawn
    LOG_INFO("[FFmpegProcessHelper] 准备启动 FFmpeg 进程:");
    LOG_INFO("[FFmpegProcessHelper]   Process ID: {}", process_id);
    LOG_INFO("[FFmpegProcessHelper]   Log File: {}", log_file);
    LOG_INFO("[FFmpegProcessHelper]   Command: {}", command);
    
    // 使用 FFmpegExecutor 启动（它会自动处理 ProcessManager 或回退到简单模式）
    pid_t process_pid = process::FFmpegExecutor::StartProcess(
        command,
        process_manager_,
        process_id,
        "",  // process_name
        "",  // source_url
        "",  // target_app
        "",  // target_stream
        log_file);
    
    if (process_pid > 0) {
        pid = process_pid;
        status = GatewayStatus::Running;
        LOG_INFO("[FFmpegProcessHelper] ✅ FFmpeg 进程启动成功 (PID: {}, process_id: {})", process_pid, process_id);
        
        // DEBUG: Verify process is still alive after 500ms
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
        if (process::ProcessMonitor::IsProcessAlive(process_pid)) {
            LOG_INFO("[FFmpegProcessHelper] ✅ FFmpeg 进程存活确认 (PID: {})", process_pid);
        } else {
            LOG_ERROR("[FFmpegProcessHelper] ❌ FFmpeg 进程已退出 (PID: {}, 启动后500ms内死亡)", process_pid);
            status = GatewayStatus::Error;
        }
        return true;
    } else {
        pid = 0;
        status = GatewayStatus::Error;
        LOG_ERROR("[FFmpegProcessHelper] ❌ FFmpeg 进程启动失败 (process_id: {}, log_file: {})", process_id, log_file);
        LOG_ERROR("[FFmpegProcessHelper] 失败命令: {}", command);
        
        // 如果日志文件存在，尝试读取错误信息
        if (!log_file.empty()) {
            // 等待一下让日志写入
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            std::ifstream file(log_file);
            if (file.is_open()) {
                std::stringstream buffer;
                buffer << file.rdbuf();
                std::string error_content = buffer.str();
                if (!error_content.empty()) {
                    // 只显示最后200字符，避免日志过长
                    std::string last_error = error_content.length() > 200 
                        ? error_content.substr(error_content.length() - 200) 
                        : error_content;
                    LOG_ERROR("[FFmpegProcessHelper] FFmpeg 错误日志 (最后200字符): {}", last_error);
                } else {
                    LOG_WARN("[FFmpegProcessHelper] FFmpeg 日志文件为空: {}", log_file);
                }
            } else {
                LOG_WARN("[FFmpegProcessHelper] 无法打开 FFmpeg 日志文件: {}", log_file);
            }
        }
        return false;
    }
}

bool FFmpegProcessHelper::StopProcess(int pid,
                                     const std::string& process_id,
                                     std::function<void(GatewayStatus)> status_callback) {
    return process::FFmpegExecutor::StopProcess(
        pid,
        process_manager_,
        process_id,
        status_callback);
}

std::string FFmpegProcessHelper::ReadErrorLog(const std::string& process_id,
                                              const std::string& log_file) {
    // 优先从 ProcessManager 获取日志文件路径
    std::string actual_log_file = log_file;
    if (process_manager_ && actual_log_file.empty()) {
        auto process_info = process_manager_->GetProcessInfo(process_id);
        if (!process_info.log_file.empty()) {
            actual_log_file = process_info.log_file;
        }
    }
    
    if (actual_log_file.empty()) {
        return "";
    }
    
    // 读取日志文件（最后500字符）
    std::ifstream file(actual_log_file);
    if (!file.is_open()) {
        return "";
    }
    
    // 读取文件内容
    std::stringstream buffer;
    buffer << file.rdbuf();
    std::string content = buffer.str();
    
    // 返回最后500字符
    if (content.length() > 500) {
        return content.substr(content.length() - 500);
    }
    return content;
}

bool FFmpegProcessHelper::IsProcessRunning(int pid) {
    return process::ProcessMonitor::IsProcessAlive(pid);
}

} // namespace utils
} // namespace gateway

