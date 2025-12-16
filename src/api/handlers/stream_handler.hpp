#ifndef API_HANDLERS_STREAM_HANDLER_HPP
#define API_HANDLERS_STREAM_HANDLER_HPP

#include <httplib.h>
#include <nlohmann/json.hpp>
#include <memory>
#include <string>

namespace streaming {
class ZLMClient;
class StreamManager;
struct StreamMetadata;  // Phase 1.3: Forward declaration for helpers
class StreamStartQueue;  // Phase 2.2: Forward declaration for async stream start
}

namespace process {
class ProcessManager;
}

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
class GB28181Gateway;
class GatewayBase;
}


#include "gateway/utils/gateway_factory.hpp"

namespace api {
namespace handlers {

/**
 * @brief 流管理请求处理器
 */
class StreamHandler {
public:
    StreamHandler(
        std::shared_ptr<streaming::ZLMClient> zlm_client,
        std::shared_ptr<streaming::StreamManager> stream_manager,
        std::shared_ptr<process::ProcessManager> process_manager,
        const gateway::utils::GatewayFactory::GatewayInstances& gateways,
        std::shared_ptr<streaming::StreamStartQueue> stream_start_queue = nullptr);

    /**
     * @brief 获取流列表
     */
    void HandleGetStreams(const httplib::Request& req, httplib::Response& res);

    /**
     * @brief 启动流
     */
    void HandleStartStream(const httplib::Request& req, httplib::Response& res);

    /**
     * @brief 停止流
     */
    void HandleStopStream(const httplib::Request& req, httplib::Response& res);

    /**
     * @brief 获取流信息
     */
    /**
     * @brief 获取流信息
     */
    void HandleGetStreamInfo(const httplib::Request& req, httplib::Response& res);

    // GB28181 流管理
    void HandleGB28181StartStream(const httplib::Request& req, httplib::Response& res);
    void HandleGB28181StopStream(const httplib::Request& req, httplib::Response& res);

    /**
     * @brief 按需创建流（用于 on_stream_not_found Hook）
     * @param app 应用名
     * @param stream 流名
     * @param source_url 源流地址
     * @param protocol 协议类型
     * @param output_protocol 输出协议
     * @return 是否成功
     */
    bool CreateStreamOnDemand(const std::string& app, const std::string& stream,
                             const std::string& source_url, const std::string& protocol,
                             const std::string& output_protocol);

private:
    std::shared_ptr<streaming::ZLMClient> zlm_client_;
    std::shared_ptr<streaming::StreamManager> stream_manager_;
    std::shared_ptr<process::ProcessManager> process_manager_;
    
    // Phase 2.2: 异步流启动队列
    std::shared_ptr<streaming::StreamStartQueue> stream_start_queue_;
    
    std::shared_ptr<gateway::RTSPGateway> rtsp_gateway_;
    std::shared_ptr<gateway::RTMPGateway> rtmp_gateway_;
    std::shared_ptr<gateway::HTTPFLVGateway> httpflv_gateway_;
    std::shared_ptr<gateway::DASHGateway> dash_gateway_;
    std::shared_ptr<gateway::HLSGateway> hls_gateway_;
    std::shared_ptr<gateway::QUICGateway> quic_gateway_;
    std::shared_ptr<gateway::ONVIFGateway> onvif_gateway_;
    std::shared_ptr<gateway::ISAPIGateway> isapi_gateway_;
    std::shared_ptr<gateway::DahuaGateway> dahua_gateway_;
    std::shared_ptr<gateway::PSIAGateway> psia_gateway_;
    std::shared_ptr<gateway::LocalCameraGateway> local_camera_gateway_;
    std::shared_ptr<gateway::GB28181Gateway> gb28181_gateway_;

    // Phase 1.3: Helper methods for HandleGetStreams optimization
    bool ShouldIncludeStream(const streaming::StreamMetadata& metadata) const;
    bool IsValidString(const std::string& s) const;
    nlohmann::json BuildStreamJson(const streaming::StreamMetadata& metadata,
                                    const std::string& app_name,
                                    const std::string& stream_name) const;

    // Phase 2.2: Gateway selection helper
    std::shared_ptr<gateway::GatewayBase> SelectGateway(const std::string& protocol);

    // Gateway 注册表：协议 -> Gateway 实例
    // 用于统一管理和查找 Gateway，避免大量的 if-else 判断
    std::map<std::string, std::shared_ptr<gateway::GatewayBase>> gateways_;
};

} // namespace handlers
} // namespace api

#endif // API_HANDLERS_STREAM_HANDLER_HPP

