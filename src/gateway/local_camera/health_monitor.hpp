#ifndef GATEWAY_LOCAL_CAMERA_HEALTH_MONITOR_HPP
#define GATEWAY_LOCAL_CAMERA_HEALTH_MONITOR_HPP

#include "gateway/base/gateway_base.hpp"
#include <string>
#include <memory>
#include <thread>
#include <atomic>
#include <mutex>
#include <vector>
#include <functional>
#include <chrono>

namespace config {
struct Config;
}

namespace streaming {
class ZLMClient;
class StreamManager;
}

namespace process {
class ProcessManager;
}

namespace api {
class WebSocketServer;
}

namespace gateway {
// 前向声明 LocalCameraStreamInfo
struct LocalCameraStreamInfo;

namespace local_camera {
// 使用 LocalCameraStreamInfo
using StreamInfo = LocalCameraStreamInfo;

/**
 * @brief 流健康监控器
 * 
 * 负责监控流的健康状态，并在流异常时自动恢复
 */
class HealthMonitor {
public:
    /**
     * @brief 流健康检查回调函数类型
     * @param stream_id 流ID
     * @param info 流信息
     * @return 是否健康
     */
    using HealthCheckCallback = std::function<bool(const std::string& stream_id, StreamInfo& info)>;
    
    /**
     * @brief 流恢复回调函数类型
     * @param source_url 源URL
     * @param target_app 目标应用名
     * @param target_stream 目标流名
     * @param output_protocol 输出协议
     * @return 是否成功
     */
    using RecoverCallback = std::function<bool(const std::string& source_url,
                                               const std::string& target_app,
                                               const std::string& target_stream,
                                               const std::string& output_protocol)>;
    
    /**
     * @brief 获取需要检查的流列表回调函数类型
     * @return 流列表（stream_id -> StreamInfo）
     */
    using GetStreamsCallback = std::function<std::vector<std::pair<std::string, StreamInfo>>()>;
    
    /**
     * @brief 更新流状态回调函数类型
     * @param stream_id 流ID
     * @param status 新状态
     */
    using UpdateStreamStatusCallback = std::function<void(const std::string& stream_id, GatewayStatus status)>;
    
    /**
     * @brief 更新恢复信息回调函数类型
     * @param stream_id 流ID
     * @param recover_attempts 恢复尝试次数
     * @param last_recover_time 最后恢复时间
     */
    using UpdateRecoverInfoCallback = std::function<void(const std::string& stream_id,
                                                         int recover_attempts,
                                                         std::chrono::system_clock::time_point last_recover_time)>;

    /**
     * @brief 构造函数
     * @param config 配置对象
     * @param zlm_client ZLMediaKit 客户端
     * @param process_manager 进程管理器（可选）
     */
    HealthMonitor(std::shared_ptr<config::Config> config,
                  std::shared_ptr<streaming::ZLMClient> zlm_client,
                  std::shared_ptr<process::ProcessManager> process_manager = nullptr);

    /**
     * @brief 析构函数
     */
    ~HealthMonitor();

    /**
     * @brief 启动健康监控
     * @param get_streams_callback 获取流列表的回调
     * @param check_health_callback 健康检查回调
     * @param update_status_callback 更新流状态回调
     * @param update_recover_info_callback 更新恢复信息回调
     * @param recover_callback 恢复流回调
     * @param websocket_server WebSocket 服务器（可选，用于通知前端）
     * @param stream_manager 流管理器（可选，用于更新流状态）
     */
    void Start(GetStreamsCallback get_streams_callback,
               HealthCheckCallback check_health_callback,
               UpdateStreamStatusCallback update_status_callback,
               UpdateRecoverInfoCallback update_recover_info_callback,
               RecoverCallback recover_callback,
               std::shared_ptr<api::WebSocketServer> websocket_server = nullptr,
               std::shared_ptr<streaming::StreamManager> stream_manager = nullptr);

    /**
     * @brief 停止健康监控
     */
    void Stop();

    /**
     * @brief 检查单个流的健康状态
     * @param stream_id 流ID
     * @param info 流信息
     * @return 是否健康
     */
    bool CheckStreamHealth(const std::string& stream_id, StreamInfo& info) const;

private:
    /**
     * @brief 健康监控线程函数
     */
    void MonitorThread();

    /**
     * @brief 清理已完成的恢复线程
     */
    void CleanupRecoverThreads();

    std::shared_ptr<config::Config> config_;
    std::shared_ptr<streaming::ZLMClient> zlm_client_;
    std::shared_ptr<process::ProcessManager> process_manager_;
    std::shared_ptr<api::WebSocketServer> websocket_server_;
    std::shared_ptr<streaming::StreamManager> stream_manager_;

    // 回调函数
    GetStreamsCallback get_streams_callback_;
    HealthCheckCallback check_health_callback_;
    UpdateStreamStatusCallback update_status_callback_;
    UpdateRecoverInfoCallback update_recover_info_callback_;
    RecoverCallback recover_callback_;

    // 监控线程
    std::thread monitor_thread_;
    std::atomic<bool> running_{false};
    int check_interval_seconds_ = 10;

    // 恢复线程管理
    std::vector<std::thread> recover_threads_;
    mutable std::mutex recover_threads_mutex_;
};

} // namespace local_camera
} // namespace gateway

#endif // GATEWAY_LOCAL_CAMERA_HEALTH_MONITOR_HPP

