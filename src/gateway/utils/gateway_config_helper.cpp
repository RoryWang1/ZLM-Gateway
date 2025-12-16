#include "gateway/utils/gateway_config_helper.hpp"
#include "config/config_loader.hpp"
#include <sstream>

namespace gateway {
namespace utils {

std::string GatewayConfigHelper::GetFFmpegPath(std::shared_ptr<config::Config> config,
                                               const std::string& default_path) {
    if (config && !config->rtsp.ffmpeg_path.empty()) {
        return config->rtsp.ffmpeg_path;
    }
    return default_path;
}

std::string GatewayConfigHelper::GetFFprobePath(std::shared_ptr<config::Config> config,
                                                const std::string& default_path) {
    if (config && !config->rtsp.ffprobe_path.empty()) {
        return config->rtsp.ffprobe_path;
    }
    return default_path;
}

std::string GatewayConfigHelper::BuildZLMRTSPURL(std::shared_ptr<config::Config> config) {
    if (!config) {
        return "rtsp://127.0.0.1:554";
    }
    
    std::ostringstream oss;
    oss << "rtsp://127.0.0.1:" << config->zlmediakit.rtsp_port;
    return oss.str();
}

} // namespace utils
} // namespace gateway

