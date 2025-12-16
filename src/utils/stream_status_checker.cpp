#include "utils/stream_status_checker.hpp"
#include "utils/logger.hpp"
#include "streaming/zlmediakit/zlm_client.hpp"

namespace utils {

bool StreamStatusChecker::CheckIsRunning(
    int pid,
    gateway::GatewayStatus current_status,
    std::shared_ptr<streaming::ZLMClient> zlm_client,
    const std::string& app,
    const std::string& stream,
    gateway::GatewayStatus& status_out,
    int& pid_out
) {
    pid_out = pid;
    status_out = current_status;

    // 如果有进程，先检查进程状态
    if (pid > 0) {
        if (!process::ProcessMonitor::IsProcessAlive(pid)) {
            // 进程已退出，检查 ZLM 中是否有流
            bool zlm_alive = IsStreamAliveInZLM(zlm_client, app, stream);
            if (zlm_alive) {
                // ZLM 中有流且活跃，可能是进程退出但流已建立
                status_out = gateway::GatewayStatus::Running;
                pid_out = 0;  // 清除 PID，因为进程已退出
                return true;
            } else {
                // 进程退出且 ZLM 中没有流，状态为 Error
                status_out = gateway::GatewayStatus::Error;
                pid_out = 0;
                return false;
            }
        }

        // 进程还在运行，检查 ZLM 中是否有流
        bool zlm_alive = IsStreamAliveInZLM(zlm_client, app, stream);
        if (zlm_alive) {
            status_out = gateway::GatewayStatus::Running;
            return true;
        } else {
            // 进程运行但 ZLM 中还没有流，可能是正在启动
            return (current_status == gateway::GatewayStatus::Running || 
                    current_status == gateway::GatewayStatus::Starting);
        }
    } else {
        // 没有进程，只检查 ZLM
        bool zlm_alive = IsStreamAliveInZLM(zlm_client, app, stream);
        if (zlm_alive) {
            status_out = gateway::GatewayStatus::Running;
            return true;
        } else {
            status_out = gateway::GatewayStatus::Stopped;
            return false;
        }
    }
}

gateway::GatewayStatus StreamStatusChecker::GetStatus(
    int pid,
    gateway::GatewayStatus current_status,
    std::shared_ptr<streaming::ZLMClient> zlm_client,
    const std::string& app,
    const std::string& stream,
    int& pid_out
) {
    pid_out = pid;

    // 如果有进程，先检查进程状态
    if (pid > 0) {
        if (!process::ProcessMonitor::IsProcessAlive(pid)) {
            // 进程已退出，检查 ZLM 中是否有流
            bool zlm_alive = IsStreamAliveInZLM(zlm_client, app, stream);
            if (zlm_alive) {
                // ZLM 中有流且活跃，可能是进程退出但流已建立
                pid_out = 0;  // 清除 PID，因为进程已退出
                return gateway::GatewayStatus::Running;
            } else {
                // 进程退出且 ZLM 中没有流，状态为 Error
                pid_out = 0;
                return gateway::GatewayStatus::Error;
            }
        }

        // 进程还在运行，检查 ZLM 中是否有流
        bool zlm_alive = IsStreamAliveInZLM(zlm_client, app, stream);
        if (zlm_alive) {
            return gateway::GatewayStatus::Running;
        } else {
            // 进程运行但 ZLM 中还没有流，可能是正在启动
            if (current_status == gateway::GatewayStatus::Starting) {
                // 保持 Starting 状态
                return gateway::GatewayStatus::Starting;
            } else {
                return gateway::GatewayStatus::Starting;
            }
        }
    } else {
        // 没有进程，只检查 ZLM
        bool zlm_alive = IsStreamAliveInZLM(zlm_client, app, stream);
        if (zlm_alive) {
            return gateway::GatewayStatus::Running;
        } else {
            return gateway::GatewayStatus::Stopped;
        }
    }
}

bool StreamStatusChecker::IsStreamAliveInZLM(
    std::shared_ptr<streaming::ZLMClient> zlm_client,
    const std::string& app,
    const std::string& stream
) {
    if (!zlm_client) {
        return false;
    }

    try {
        auto stream_info = zlm_client->GetStreamInfo(app, stream);
        // 使用 IsStreamActive 方法，它不仅检查 alive 字段，还检查 bytes_speed 和 total_bytes
        // 这对于直接代理模式（没有进程）的流很重要
        return streaming::ZLMClient::IsStreamActive(stream_info);
    } catch (const std::exception& e) {
        LOG_WARN("检查 ZLM 流状态失败: {}/{} - {}", app, stream, e.what());
        return false;
    }
}

} // namespace utils

