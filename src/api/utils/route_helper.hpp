#ifndef API_UTILS_ROUTE_HELPER_HPP
#define API_UTILS_ROUTE_HELPER_HPP

#include <httplib.h>
#include <functional>
#include <string>

namespace api {
namespace utils {

/**
 * @brief 路由注册辅助工具
 * 
 * 提供简化的路由注册方法，减少重复代码
 */
class RouteHelper {
public:
    /**
     * @brief 注册GET路由
     * @param server HTTP服务器
     * @param path 路由路径
     * @param handler 处理函数
     */
    static void RegisterGet(httplib::Server* server, 
                           const std::string& path,
                           std::function<void(const httplib::Request&, httplib::Response&)> handler);

    /**
     * @brief 注册POST路由
     * @param server HTTP服务器
     * @param path 路由路径
     * @param handler 处理函数
     */
    static void RegisterPost(httplib::Server* server,
                            const std::string& path,
                            std::function<void(const httplib::Request&, httplib::Response&)> handler);

    /**
     * @brief 注册DELETE路由
     * @param server HTTP服务器
     * @param path 路由路径
     * @param handler 处理函数
     */
    static void RegisterDelete(httplib::Server* server,
                              const std::string& path,
                              std::function<void(const httplib::Request&, httplib::Response&)> handler);
};

} // namespace utils
} // namespace api

#endif // API_UTILS_ROUTE_HELPER_HPP

