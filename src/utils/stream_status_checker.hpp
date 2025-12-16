#ifndef UTILS_STREAM_STATUS_CHECKER_HPP
#define UTILS_STREAM_STATUS_CHECKER_HPP

#include "gateway/base/gateway_base.hpp"
#include "streaming/zlmediakit/zlm_client.hpp"
#include "process/process_monitor.hpp"
#include <memory>

namespace utils {

/**
 * @brief 流状态检查工具类
 * 
 * 封装了检查流状态的公共逻辑，包括：
 * - 进程状态检查
 * - ZLMediaKit 流状态检查
 * - 状态更新逻辑
 */
class StreamStatusChecker {
public:
    /**
     * @brief 检查流是否运行中
     * @param pid 进程 PID（如果 > 0 则检查进程状态）
     * @param current_status 当前状态
     * @param zlm_client ZLMediaKit 客户端
     * @param app 应用名
     * @param stream 流名
     * @param status_out 输出参数：更新后的状态
     * @param pid_out 输出参数：更新后的 PID（如果进程已退出但流还在，设为0）
     * @return 是否运行中
     */
    static bool CheckIsRunning(
        int pid,
        gateway::GatewayStatus current_status,
        std::shared_ptr<streaming::ZLMClient> zlm_client,
        const std::string& app,
        const std::string& stream,
        gateway::GatewayStatus& status_out,
        int& pid_out
    );

    /**
     * @brief 获取流状态
     * @param pid 进程 PID（如果 > 0 则检查进程状态）
     * @param current_status 当前状态
     * @param zlm_client ZLMediaKit 客户端
     * @param app 应用名
     * @param stream 流名
     * @param pid_out 输出参数：更新后的 PID（如果进程已退出但流还在，设为0）
     * @return 流状态
     */
    static gateway::GatewayStatus GetStatus(
        int pid,
        gateway::GatewayStatus current_status,
        std::shared_ptr<streaming::ZLMClient> zlm_client,
        const std::string& app,
        const std::string& stream,
        int& pid_out
    );

private:
    /**
     * @brief 检查 ZLMediaKit 中流是否活跃
     * @param zlm_client ZLMediaKit 客户端
     * @param app 应用名
     * @param stream 流名
     * @return 是否活跃
     */
    static bool IsStreamAliveInZLM(
        std::shared_ptr<streaming::ZLMClient> zlm_client,
        const std::string& app,
        const std::string& stream
    );
};

} // namespace utils

#endif // UTILS_STREAM_STATUS_CHECKER_HPP

