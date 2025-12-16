#ifndef API_UTILS_RESPONSE_HELPER_HPP
#define API_UTILS_RESPONSE_HELPER_HPP

#include <httplib.h>
#include <nlohmann/json.hpp>
#include "common/exceptions.hpp"

namespace api {
namespace utils {

/**
 * @brief HTTP响应辅助工具
 * 
 * 提供统一的响应构建方法，减少重复代码
 */
class ResponseHelper {
public:
    /**
     * @brief 返回成功响应
     * @param res HTTP响应对象
     * @param data 响应数据
     * @param msg 可选的消息
     */
    static void Success(httplib::Response& res, const nlohmann::json& data, const std::string& msg = "");

    /**
     * @brief 返回成功响应（空数据）
     * @param res HTTP响应对象
     * @param msg 可选的消息
     */
    static void Success(httplib::Response& res, const std::string& msg = "");

    /**
     * @brief 返回错误响应
     * @param res HTTP响应对象
     * @param code 错误码
     * @param msg 错误消息
     * @param status HTTP状态码（默认500）
     */
    static void Error(httplib::Response& res, int code, const std::string& msg, int status = 500);

    /**
     * @brief 返回错误响应（使用异常消息）
     * @param res HTTP响应对象
     * @param e 异常对象
     * @param status HTTP状态码（默认500）
     */
    static void Error(httplib::Response& res, const std::exception& e, int status = 500);

    /**
     * @brief 返回错误响应（使用GatewayException）
     * @param res HTTP响应对象
     * @param e GatewayException对象
     */
    static void Error(httplib::Response& res, const gateway::GatewayException& e);

    /**
     * @brief 返回参数错误响应
     * @param res HTTP响应对象
     * @param msg 错误消息
     */
    static void BadRequest(httplib::Response& res, const std::string& msg);

    /**
     * @brief 返回未找到响应
     * @param res HTTP响应对象
     * @param msg 错误消息
     */
    static void NotFound(httplib::Response& res, const std::string& msg);

    /**
     * @brief 返回冲突响应（如资源已存在）
     * @param res HTTP响应对象
     * @param msg 错误消息
     */
    static void Conflict(httplib::Response& res, const std::string& msg);

};

} // namespace utils
} // namespace api

#endif // API_UTILS_RESPONSE_HELPER_HPP

