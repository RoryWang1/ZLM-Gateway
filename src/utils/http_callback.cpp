#include "utils/http_callback.hpp"
#include "utils/logger.hpp"
#include <string>

namespace utils {
namespace http_callback {

size_t WriteCallback(void* contents, size_t size, size_t nmemb, void* userdata) {
    if (!contents || size == 0 || nmemb == 0 || !userdata) {
        return 0;
    }
    
    std::string* data = static_cast<std::string*>(userdata);
    try {
        size_t total_size = size * nmemb;
        data->append(static_cast<const char*>(contents), total_size);
        return total_size;
    } catch (const std::exception& e) {
        LOG_ERROR("HTTP 写入回调异常: {}", e.what());
        return 0;
    } catch (...) {
        LOG_ERROR("HTTP 写入回调未知异常");
        return 0;
    }
}

} // namespace http_callback
} // namespace utils

