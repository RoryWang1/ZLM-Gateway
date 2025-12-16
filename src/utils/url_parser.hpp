#ifndef UTILS_URL_PARSER_HPP
#define UTILS_URL_PARSER_HPP

#include <string>
#include <utility>

namespace utils {

/**
 * @brief URL 解析工具
 * 
 * 提供通用的 URL 解析功能，消除各 gateway 中的重复代码
 */
namespace url_parser {

/**
 * @brief 解析设备协议 URL（格式：protocol://device_id/channel_id）
 * @param url 完整的 URL
 * @param protocol_prefix 协议前缀（如 "psia://", "isapi://"）
 * @return pair<device_id, channel_id>，如果解析失败则返回空字符串
 */
std::pair<std::string, std::string> ParseDeviceURL(const std::string& url, const std::string& protocol_prefix);

/**
 * @brief 验证 URL 格式
 * @param url 完整的 URL
 * @param protocol_prefix 协议前缀
 * @return 是否有效
 */
bool ValidateURL(const std::string& url, const std::string& protocol_prefix);

} // namespace url_parser
} // namespace utils

#endif // UTILS_URL_PARSER_HPP

