#ifndef GATEWAY_UTILS_GATEWAY_REGISTRY_HPP
#define GATEWAY_UTILS_GATEWAY_REGISTRY_HPP

#include <memory>
#include <string>
#include <map>
#include <functional>
#include <mutex>
#include "gateway/base/gateway_base.hpp"

// Forward declarations to avoid heavy includes
namespace config { struct Config; }
namespace streaming { class ZLMClient; class StreamManager; }
namespace process { class ProcessManager; }
namespace utils { class BitrateAllocator; }
namespace api { class WebSocketServer; }

namespace gateway {
namespace utils {

/**
 * @brief Gateway Creation Context
 * Contains all dependencies needed to create a gateway instance.
 */
struct GatewayContext {
    std::shared_ptr<config::Config> config;
    std::shared_ptr<streaming::ZLMClient> zlm_client;
    std::shared_ptr<process::ProcessManager> process_manager;
    std::shared_ptr<streaming::StreamManager> stream_manager;
    std::shared_ptr<::utils::BitrateAllocator> bitrate_allocator;
    std::shared_ptr<api::WebSocketServer> websocket_server;
};

/**
 * @brief Function type for creating a Gateway instance
 */
using GatewayCreator = std::function<std::shared_ptr<GatewayBase>(const GatewayContext&)>;

/**
 * @brief Singleton Registry for Gateways
 * Allows generic creation of registered gateways.
 */
class GatewayRegistry {
public:
    static GatewayRegistry& Instance() {
        static GatewayRegistry instance;
        return instance;
    }

    // Register a gateway creator
    void Register(const std::string& name, GatewayCreator creator) {
        std::lock_guard<std::mutex> lock(mutex_);
        creators_[name] = creator;
    }

    // Create a specific gateway by name
    std::shared_ptr<GatewayBase> Create(const std::string& name, const GatewayContext& context) {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = creators_.find(name);
        if (it != creators_.end()) {
            return it->second(context);
        }
        return nullptr;
    }

    // Get all registered names
    std::vector<std::string> GetRegisteredNames() const {
        std::lock_guard<std::mutex> lock(mutex_);
        std::vector<std::string> names;
        for (const auto& pair : creators_) {
            names.push_back(pair.first);
        }
        return names;
    }

private:
    GatewayRegistry() = default;
    ~GatewayRegistry() = default;
    GatewayRegistry(const GatewayRegistry&) = delete;
    GatewayRegistry& operator=(const GatewayRegistry&) = delete;

    std::map<std::string, GatewayCreator> creators_;
    mutable std::mutex mutex_;
};

/**
 * @brief Helper class for static registration
 */
class AutoRegister {
public:
    AutoRegister(const std::string& name, GatewayCreator creator) {
        GatewayRegistry::Instance().Register(name, creator);
    }
};

} // namespace utils
} // namespace gateway

#endif // GATEWAY_UTILS_GATEWAY_REGISTRY_HPP
