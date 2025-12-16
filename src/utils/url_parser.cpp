#include "utils/url_parser.hpp"
#include "utils/logger.hpp"
#include <algorithm>

namespace utils {
namespace url_parser {

std::pair<std::string, std::string> ParseDeviceURL(const std::string& url, const std::string& protocol_prefix) {
    if (url.find(protocol_prefix) != 0) {
        return {"", ""};
    }
    
    std::string url_path = url.substr(protocol_prefix.length());
    size_t slash_pos = url_path.find('/');
    
    std::string device_id = url_path;
    std::string channel_id = "";
    
    if (slash_pos != std::string::npos) {
        device_id = url_path.substr(0, slash_pos);
        channel_id = url_path.substr(slash_pos + 1);
    }
    
    return {device_id, channel_id};
}

bool ValidateURL(const std::string& url, const std::string& protocol_prefix) {
    return url.find(protocol_prefix) == 0;
}

} // namespace url_parser
} // namespace utils

