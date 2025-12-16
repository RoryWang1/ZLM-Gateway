#ifndef API_HTTP_SERVER_HPP
#define API_HTTP_SERVER_HPP

#include <string>
#include <memory>
#include <functional>
#include <mutex>
#include <atomic>
#include <chrono>
#include <thread>
#include <httplib.h>
#include "config/config_loader.hpp"
#include "streaming/zlmediakit/zlm_client.hpp"
#include "streaming/stream_manager.hpp"
#include "streaming/stream_start_queue.hpp"
#include "monitoring/statistics_manager.hpp"


#include "api/handlers/stream_handler.hpp"
#include "api/handlers/device_handler.hpp"
#include "api/handlers/statistics_handler.hpp"
#include "api/handlers/hook_handler.hpp"
#include "api/handlers/process_handler.hpp"

namespace gateway {
class RTSPGateway;
class RTMPGateway;
class HTTPFLVGateway;
class DASHGateway;
class HLSGateway;
class QUICGateway;
class ONVIFGateway;
class ISAPIGateway;
class DahuaGateway;
class PSIAGateway;
class LocalCameraGateway;
}


namespace process {
class ProcessManager;
}

#include "gateway/utils/gateway_factory.hpp"

namespace api {

/**
 * @brief HTTP API 服务器
 */
class HttpServer {
public:
    /**
     * @brief 构造函数
     * @param config 配置对象
     * @param zlm_client ZLMediaKit 客户端
     * @param stream_manager 流管理器
     * @param process_manager 进程管理器
     * @param gateways Gateway实例集合
     */
    HttpServer(std::shared_ptr<config::Config> config,
               std::shared_ptr<streaming::ZLMClient> zlm_client,
               std::shared_ptr<streaming::StreamManager> stream_manager,
               std::shared_ptr<process::ProcessManager> process_manager,
               const gateway::utils::GatewayFactory::GatewayInstances& gateways);

    /**
     * @brief 启动服务器
     * @return 是否成功
     */
    bool Start();

    /**
     * @brief 停止服务器
     */
    void Stop();

    /**
     * @brief 是否正在运行
     */
    bool IsRunning() const { return is_running_; }

private:
    /**
     * @brief 初始化HTTP服务器配置
     */
    void InitializeServer();

    /**
     * @brief 初始化监控和告警系统
     */
    void InitializeMonitoring();

    /**
     * @brief 初始化所有Handler
     */
    void InitializeHandlers();

    /**
     * @brief 注册路由
     */
    void RegisterRoutes();

    /**
     * @brief 健康检查
     */
    void HandleHealth(const httplib::Request& req, httplib::Response& res);

    // GB28181 设备管理 API 处理函数已移至 DeviceHandler 和 StreamHandler

    std::shared_ptr<config::Config> config_;
    std::shared_ptr<streaming::ZLMClient> zlm_client_;
    std::shared_ptr<streaming::StreamManager> stream_manager_;
    std::shared_ptr<process::ProcessManager> process_manager_;
    std::shared_ptr<monitoring::StatisticsManager> statistics_manager_;
    
    // Phase 2.2: 异步流启动队列 (shared_ptr for sharing across handlers)
    std::shared_ptr<streaming::StreamStartQueue> stream_start_queue_;
    
    // Phase 6.2: Universal Gateway Instances
    gateway::utils::GatewayFactory::GatewayInstances gateways_;
    
    std::unique_ptr<httplib::Server> server_;
    std::thread server_thread_;  // Manage the server thread
    std::shared_ptr<handlers::StreamHandler> stream_handler_;
    std::shared_ptr<handlers::DeviceHandler> device_handler_;
    std::shared_ptr<handlers::StatisticsHandler> statistics_handler_;
    std::shared_ptr<handlers::HookHandler> hook_handler_;
    std::shared_ptr<handlers::ProcessHandler> process_handler_;
    bool is_running_ = false;
    int port_ = 8080;
    
    // ZLM 在线状态缓存（避免每次健康检查都调用 IsOnline）
    std::atomic<bool> cached_zlm_online_{false};
    std::chrono::steady_clock::time_point last_zlm_check_time_;
    std::mutex zlm_check_mutex_;
    static constexpr int ZLM_CHECK_INTERVAL_SECONDS = 5; // 每 5 秒检查一次
};

} // namespace api

#endif // API_HTTP_SERVER_HPP

