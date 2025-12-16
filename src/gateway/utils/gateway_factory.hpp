#ifndef GATEWAY_UTILS_GATEWAY_FACTORY_HPP
#define GATEWAY_UTILS_GATEWAY_FACTORY_HPP

#include <memory>
#include <map>
#include <string>
#include "config/config_loader.hpp"
#include "gateway/base/gateway_base.hpp"

namespace streaming {
class ZLMClient;
class StreamManager;
}

namespace process {
class ProcessManager;
}

namespace utils {
class BitrateAllocator;
}

namespace api {
class WebSocketServer;
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

namespace utils {

/**
 * @brief Gateway工厂类
 * 
 * 统一创建和管理所有Gateway实例，减少main.cpp中的重复代码
 */
class GatewayFactory {
public:
    /**
     * @brief Gateway创建结果
     */
    /**
     * @brief Gateway创建结果
     */
    class GatewayInstances {
    public:
        // Generic accessor
        template <typename T>
        std::shared_ptr<T> Get(const std::string& name) const {
            auto it = gateways_.find(name);
            if (it != gateways_.end()) {
                return std::dynamic_pointer_cast<T>(it->second);
            }
            return nullptr;
        }

        // Add a gateway
        void Add(const std::string& name, std::shared_ptr<GatewayBase> gateway) {
            gateways_[name] = gateway;
        }

        // Access underlying map if needed (e.g. for iteration)
        const std::map<std::string, std::shared_ptr<GatewayBase>>& GetAll() const {
            return gateways_;
        }

    private:
        std::map<std::string, std::shared_ptr<GatewayBase>> gateways_;
    };

    /**
     * @brief 创建所有Gateway实例
     * @param config 配置对象
     * @param zlm_client ZLM客户端
     * @param process_manager 进程管理器
     * @param stream_manager 流管理器
     * @param bitrate_allocator 码率分配器
     * @param websocket_server WebSocket服务器（可选，用于LocalCameraGateway）
     * @return Gateway实例集合
     */
    static GatewayInstances CreateAllGateways(
        std::shared_ptr<config::Config> config,
        std::shared_ptr<streaming::ZLMClient> zlm_client,
        std::shared_ptr<process::ProcessManager> process_manager,
        std::shared_ptr<streaming::StreamManager> stream_manager,
        std::shared_ptr<::utils::BitrateAllocator> bitrate_allocator,
        std::shared_ptr<api::WebSocketServer> websocket_server = nullptr);
};

} // namespace utils
} // namespace gateway

#endif // GATEWAY_UTILS_GATEWAY_FACTORY_HPP

