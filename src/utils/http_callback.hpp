#ifndef UTILS_HTTP_CALLBACK_HPP
#define UTILS_HTTP_CALLBACK_HPP

#include <cstddef>
#include <string>

namespace utils {

/**
 * @brief HTTP 回调函数工具
 * 
 * 提供通用的 HTTP 请求回调函数，消除各 gateway 中的重复代码
 */
namespace http_callback {

/**
 * @brief CURL 写回调函数
 * 
 * 通用的 HTTP 响应数据写入回调，用于 CURL 请求
 * 
 * @param contents 接收到的数据
 * @param size 数据块大小
 * @param nmemb 数据块数量
 * @param userdata 用户数据指针（应指向 std::string*）
 * @return 实际写入的字节数
 */
size_t WriteCallback(void* contents, size_t size, size_t nmemb, void* userdata);

} // namespace http_callback
} // namespace utils

#endif // UTILS_HTTP_CALLBACK_HPP

