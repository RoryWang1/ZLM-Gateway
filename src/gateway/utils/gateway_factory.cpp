#include "gateway/utils/gateway_factory.hpp"
#include "gateway/utils/gateway_factory.hpp"
#include "gateway/utils/gateway_registry.hpp"
#include "utils/logger.hpp"

namespace gateway {
namespace utils {

GatewayFactory::GatewayInstances GatewayFactory::CreateAllGateways(
    std::shared_ptr<config::Config> config,
    std::shared_ptr<streaming::ZLMClient> zlm_client,
    std::shared_ptr<process::ProcessManager> process_manager,
    std::shared_ptr<streaming::StreamManager> stream_manager,
    std::shared_ptr<::utils::BitrateAllocator> bitrate_allocator,
    std::shared_ptr<api::WebSocketServer> websocket_server) {
    
    GatewayInstances instances;

    GatewayContext context;
    context.config = config;
    context.zlm_client = zlm_client;
    context.process_manager = process_manager;
    context.stream_manager = stream_manager;
    context.bitrate_allocator = bitrate_allocator;
    context.websocket_server = websocket_server;

    auto names = GatewayRegistry::Instance().GetRegisteredNames();
    for (const auto& name : names) {
        auto gateway = GatewayRegistry::Instance().Create(name, context);
        if (gateway) { // Create returns nullptr if disabled in creator (or not found)
            instances.Add(name, gateway);
            // Logger info is moved to individual creators or logged here generic
             ::utils::Logger::Get()->info("Gateway verified and registered: {}", name);
        }
    }

    return instances;
}

} // namespace utils
} // namespace gateway

