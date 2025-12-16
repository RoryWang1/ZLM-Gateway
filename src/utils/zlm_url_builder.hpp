#ifndef UTILS_ZLM_URL_BUILDER_HPP
#define UTILS_ZLM_URL_BUILDER_HPP

#include <string>
#include <memory>
#include <sstream>

namespace config {
struct Config;
}

namespace utils {

/**
 * @brief ZLMediaKit URL 构建器
 * 
 * 提供统一的 ZLMediaKit 推流 URL 构建功能，消除各 gateway 中的重复代码
 */
namespace zlm_url_builder {

/**
 * @brief 安全地获取 ZLMediaKit secret
 * @param config 配置对象
 * @param gateway_name Gateway 名称（用于日志）
 * @return secret 字符串，失败返回空字符串
 */
std::string GetSecret(std::shared_ptr<config::Config> config, const std::string& gateway_name = "");

/**
 * @brief 构建 ZLMediaKit RTMP 推流 URL
 * @param config 配置对象
 * @param target_app 目标应用名
 * @param target_stream 目标流名
 * @param gateway_name Gateway 名称（用于日志）
 * @return 完整的 RTMP 推流 URL
 */
std::string BuildRTMPUrl(std::shared_ptr<config::Config> config,
                         const std::string& target_app,
                         const std::string& target_stream,
                         const std::string& gateway_name = "");

/**
 * @brief 构建 ZLMediaKit RTSP 推流 URL
 * @param config 配置对象
 * @param target_app 目标应用名
 * @param target_stream 目标流名
 * @param gateway_name Gateway 名称（用于日志）
 * @return 完整的 RTSP 推流 URL
 */
std::string BuildRTSPUrl(std::shared_ptr<config::Config> config,
                         const std::string& target_app,
                         const std::string& target_stream,
                         const std::string& gateway_name = "");

/**
 * @brief 在 URL 后追加 secret 参数
 * @param url 基础 URL
 * @param secret secret 字符串
 * @return 追加 secret 后的完整 URL
 */
std::string AppendSecret(const std::string& url, const std::string& secret);

/**
 * @brief 构建带 secret 的 ZLMediaKit RTMP 推流 URL
 * @param config 配置对象
 * @param target_app 目标应用名
 * @param target_stream 目标流名
 * @param gateway_name Gateway 名称（用于日志）
 * @return 完整的带 secret 的 RTMP 推流 URL
 */
std::string BuildRTMPUrlWithSecret(std::shared_ptr<config::Config> config,
                                   const std::string& target_app,
                                   const std::string& target_stream,
                                   const std::string& gateway_name = "");

/**
 * @brief 构建带 secret 的 ZLMediaKit RTSP 推流 URL
 * @param config 配置对象
 * @param target_app 目标应用名
 * @param target_stream 目标流名
 * @param gateway_name Gateway 名称（用于日志）
 * @return 完整的带 secret 的 RTSP 推流 URL
 */
std::string BuildRTSPUrlWithSecret(std::shared_ptr<config::Config> config,
                                   const std::string& target_app,
                                   const std::string& target_stream,
                                   const std::string& gateway_name = "");

} // namespace zlm_url_builder
} // namespace utils

#endif // UTILS_ZLM_URL_BUILDER_HPP

