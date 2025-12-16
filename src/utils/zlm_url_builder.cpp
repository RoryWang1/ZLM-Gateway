#include "utils/zlm_url_builder.hpp"
#include "config/config_loader.hpp"
#include "utils/logger.hpp"
#include <sstream>

namespace utils {
namespace zlm_url_builder {

std::string GetSecret(std::shared_ptr<config::Config> config, const std::string& gateway_name) {
    if (!config) {
        if (!gateway_name.empty()) {
            LOG_WARN("[{}] Config pointer is NULL", gateway_name);
        }
        return "";
    }
    
    try {
        return config->zlmediakit.secret;
    } catch (const std::exception& e) {
        if (!gateway_name.empty()) {
            LOG_ERROR("[{}] Failed to access config secret: {}", gateway_name, e.what());
        }
        return "";
    }
}

std::string BuildRTMPUrl(std::shared_ptr<config::Config> config,
                         const std::string& target_app,
                         const std::string& target_stream,
                         const std::string& /* gateway_name */) {
    if (!config) {
        return "";
    }
    
    std::ostringstream oss;
    oss << "rtmp://127.0.0.1:" << config->zlmediakit.rtmp_port
        << "/" << target_app << "/" << target_stream;
    return oss.str();
}

std::string BuildRTSPUrl(std::shared_ptr<config::Config> config,
                         const std::string& target_app,
                         const std::string& target_stream,
                         const std::string& /* gateway_name */) {
    if (!config) {
        return "";
    }
    
    std::ostringstream oss;
    oss << "rtsp://127.0.0.1:" << config->zlmediakit.rtsp_port
        << "/" << target_app << "/" << target_stream;
    return oss.str();
}

std::string AppendSecret(const std::string& url, const std::string& secret) {
    if (secret.empty()) {
        return url;
    }
    
    std::ostringstream oss;
    oss << url << "?secret=" << secret;
    return oss.str();
}

std::string BuildRTMPUrlWithSecret(std::shared_ptr<config::Config> config,
                                   const std::string& target_app,
                                   const std::string& target_stream,
                                   const std::string& gateway_name) {
    std::string url = BuildRTMPUrl(config, target_app, target_stream, gateway_name);
    std::string secret = GetSecret(config, gateway_name);
    return AppendSecret(url, secret);
}

std::string BuildRTSPUrlWithSecret(std::shared_ptr<config::Config> config,
                                   const std::string& target_app,
                                   const std::string& target_stream,
                                   const std::string& gateway_name) {
    std::string url = BuildRTSPUrl(config, target_app, target_stream, gateway_name);
    std::string secret = GetSecret(config, gateway_name);
    return AppendSecret(url, secret);
}

} // namespace zlm_url_builder
} // namespace utils

