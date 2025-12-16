#ifndef GATEWAY_UTILS_GATEWAY_CONFIG_HELPER_HPP
#define GATEWAY_UTILS_GATEWAY_CONFIG_HELPER_HPP

#include <string>
#include <memory>

namespace config {
struct Config;
}

namespace gateway {
namespace utils {

/**
 * @brief Gateway配置辅助工具
 * 
 * 提供统一的配置获取方法，减少重复代码
 */
class GatewayConfigHelper {
public:
    /**
     * @brief 获取FFmpeg路径
     * @param config 配置对象
     * @param default_path 默认路径（如果配置为空）
     * @return FFmpeg路径
     */
    static std::string GetFFmpegPath(std::shared_ptr<config::Config> config, 
                                     const std::string& default_path = "ffmpeg");

    /**
     * @brief 获取FFprobe路径
     * @param config 配置对象
     * @param default_path 默认路径（如果配置为空）
     * @return FFprobe路径
     */
    static std::string GetFFprobePath(std::shared_ptr<config::Config> config,
                                     const std::string& default_path = "third_party/ffmpeg/macos-arm64/ffprobe");

    /**
     * @brief 构建ZLMediaKit RTSP推流地址
     * @param config 配置对象
     * @return RTSP推流地址
     */
    static std::string BuildZLMRTSPURL(std::shared_ptr<config::Config> config);
};

} // namespace utils
} // namespace gateway

#endif // GATEWAY_UTILS_GATEWAY_CONFIG_HELPER_HPP

