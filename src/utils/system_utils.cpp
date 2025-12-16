#include "utils/system_utils.hpp"
#include "utils/logger.hpp"
#include <unistd.h>
#include <signal.h>
#include <sys/types.h>
#include <sstream>
#include <cstdlib>
#include <thread>
#include <chrono>
#include <vector>
#include <iostream>

namespace utils {

void SystemUtils::CheckAndCleanPort(int port) {
    // 获取当前进程ID
    pid_t current_pid = getpid();
    
    // 使用 lsof 检查端口占用情况（macOS/Linux 通用方法）
    std::ostringstream cmd;
    cmd << "lsof -ti:" << port << " 2>/dev/null";
    
    FILE* pipe = popen(cmd.str().c_str(), "r");
    if (pipe) {
        char buffer[128];
        std::string result;
        while (fgets(buffer, sizeof(buffer), pipe) != nullptr) {
            result += buffer;
        }
        pclose(pipe);
        
        if (!result.empty()) {
            std::istringstream iss(result);
            std::string pid_str;
            
            while (iss >> pid_str) {
                try {
                    pid_t pid = std::stoi(pid_str);
                    
                    // 如果是当前进程，忽略
                    if (pid == current_pid) {
                        continue;
                    }
                    
                    // 检查进程名称
                    std::ostringstream ps_cmd;
                    ps_cmd << "ps -p " << pid << " -o comm= 2>/dev/null";
                    FILE* ps_pipe = popen(ps_cmd.str().c_str(), "r");
                    bool is_gateway_manager = false;
                    
                    if (ps_pipe) {
                        char ps_buffer[128];
                        if (fgets(ps_buffer, sizeof(ps_buffer), ps_pipe) != nullptr) {
                            std::string comm(ps_buffer);
                            if (comm.find("gateway_manager") != std::string::npos) {
                                is_gateway_manager = true;
                            }
                        }
                        pclose(ps_pipe);
                    }
                    
                    // 总是清理占用端口的进程，除非是系统关键进程（这里假设非当前进程的占用都是异常）
                    // 特别是如果是 gateway_manager 的旧实例，必须清理
                    if (is_gateway_manager) {
                        Logger::Get()->warn("检测到旧的 Gateway 实例 (PID: {}) 占用端口 {}，正在清理...", pid, port);
                    } else {
                        Logger::Get()->warn("检测到未知进程 (PID: {}) 占用端口 {}，尝试清理...", pid, port);
                    }
                    
                    // 发送 SIGTERM
                    kill(pid, SIGTERM);
                    
                    // 等待一小段时间
                    std::this_thread::sleep_for(std::chrono::milliseconds(200));
                    
                    // 检查进程是否还在
                    if (kill(pid, 0) == 0) {
                        // 进程还在，发送 SIGKILL
                        Logger::Get()->warn("进程 {} 未响应 SIGTERM，发送 SIGKILL", pid);
                        kill(pid, SIGKILL);
                        // 等待一段时间让操作系统释放端口
                        std::this_thread::sleep_for(std::chrono::milliseconds(200));
                    }
                    
                } catch (const std::exception& e) {
                    Logger::Get()->error("清理端口 {} 时发生错误: {}", port, e.what());
                }
            }
        }
    }
}

} // namespace utils
