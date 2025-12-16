#ifndef API_HANDLERS_DEVICE_HANDLER_HELPER_HPP
#define API_HANDLERS_DEVICE_HANDLER_HELPER_HPP

#include <httplib.h>
#include <nlohmann/json.hpp>
#include <memory>
#include <functional>
#include <vector>
#include <string>
#include "api/utils/response_helper.hpp"
#include "utils/logger.hpp"

using json = nlohmann::json;

namespace api {
namespace handlers {

/**
 * @brief 设备处理器助手类
 * 
 * 提供模板函数消除设备操作中的重复代码
 */
class DeviceHandlerHelper {
public:
    /**
     * @brief 通用设备发现处理
     * @tparam TGateway Gateway类型
     * @tparam TDevice 设备类型
     * @param gateway Gateway实例
     * @param protocol_name 协议名称（用于错误消息）
     * @param req HTTP请求
     * @param res HTTP响应
     * @param discover_func 发现函数（接收Gateway和Request，返回设备列表）
     * @param to_json_func 设备转JSON函数
     */
    template<typename TGateway, typename TDevice>
    static void HandleDiscover(
        std::shared_ptr<TGateway> gateway,
        const std::string& protocol_name,
        const httplib::Request& req,
        httplib::Response& res,
        std::function<std::vector<TDevice>(TGateway*, const httplib::Request&)> discover_func,
        std::function<json(const TDevice&)> to_json_func
    ) {
        try {
            if (!gateway) {
                api::utils::ResponseHelper::Error(res, -1, protocol_name + " Gateway not enabled", 500);
                return;
            }
            
            auto devices = discover_func(gateway.get(), req);
            
            json response_data = {
                {"devices", json::array()},
                {"count", devices.size()}
            };
            
            for (const auto& device : devices) {
                response_data["devices"].push_back(to_json_func(device));
            }
            
            api::utils::ResponseHelper::Success(res, response_data, "Device discovery completed");
        } catch (const gateway::GatewayException& e) {
            ::utils::Logger::Get()->error("[DeviceHandler][{}] Discover devices failed: {}", protocol_name, e.what());
            api::utils::ResponseHelper::Error(res, e.code(), e.what(), 200);
        } catch (const json::parse_error& e) {
            ::utils::Logger::Get()->error("[DeviceHandler][{}] Parse JSON failed: {}", protocol_name, e.what());
            api::utils::ResponseHelper::BadRequest(res, "Invalid JSON format");
        } catch (const std::exception& e) {
            ::utils::Logger::Get()->error("[DeviceHandler][{}] Discover devices failed: {}", protocol_name, e.what());
            api::utils::ResponseHelper::Error(res, e);
        }
    }
    
    // ... HandleList ...
    template<typename TGateway, typename TDevice>
    static void HandleList(
        std::shared_ptr<TGateway> gateway,
        const std::string& protocol_name,
        httplib::Response& res,
        std::function<std::vector<TDevice>(TGateway*)> list_func,
        std::function<json(const TDevice&)> to_json_func
    ) {
        try {
            if (!gateway) {
                // Ideally this should throw, but for now we keep as is or throw ServiceUnavailable
                throw gateway::InternalServerException(protocol_name + " Gateway not enabled");
            }
            
            auto devices = list_func(gateway.get());
            
            json response_data = {
                {"devices", json::array()},
                {"count", devices.size()}
            };
            
            for (const auto& device : devices) {
                response_data["devices"].push_back(to_json_func(device));
            }
            
            api::utils::ResponseHelper::Success(res, response_data, "success");
        } catch (const gateway::GatewayException& e) {
            ::utils::Logger::Get()->error("[DeviceHandler][{}] Get devices failed: {}", protocol_name, e.what());
            api::utils::ResponseHelper::Error(res, e.code(), e.what(), 200);
        } catch (const std::exception& e) {
            ::utils::Logger::Get()->error("[DeviceHandler][{}] Get devices failed: {}", protocol_name, e.what());
            api::utils::ResponseHelper::Error(res, e);
        }
    }
    
    // ... HandleGet ...
    template<typename TGateway, typename TDevice>
    static void HandleGet(
        std::shared_ptr<TGateway> gateway,
        const std::string& protocol_name,
        const std::string& device_id,
        httplib::Response& res,
        std::function<TDevice(TGateway*, const std::string&)> get_func,
        std::function<json(const TDevice&)> to_json_func,
        std::function<bool(const TDevice&)> is_valid_func
    ) {
        try {
            if (!gateway) {
                throw gateway::InternalServerException(protocol_name + " Gateway not enabled");
            }
            
            auto device = get_func(gateway.get(), device_id);
            
            if (!is_valid_func(device)) {
                throw gateway::DeviceNotFoundException(device_id);
            }
            
            json device_json = to_json_func(device);
            api::utils::ResponseHelper::Success(res, device_json, "success");
        } catch (const gateway::GatewayException& e) {
             ::utils::Logger::Get()->error("[DeviceHandler][{}] Get device failed: {}", protocol_name, e.what());
            api::utils::ResponseHelper::Error(res, e.code(), e.what(), 200);
        } catch (const std::exception& e) {
            ::utils::Logger::Get()->error("[DeviceHandler][{}] Get device failed: {}", protocol_name, e.what());
            api::utils::ResponseHelper::Error(res, e);
        }
    }

    // ... HandleAdd ...
    template<typename TGateway, typename TDevice>
    static void HandleAdd(
        std::shared_ptr<TGateway> gateway,
        const std::string& protocol_name,
        const httplib::Request& req,
        httplib::Response& res,
        std::function<TDevice(const json&)> parse_func,
        std::function<std::string(TGateway*, const TDevice&)> add_func,
        std::function<json(const std::string&, const TDevice&)> response_func
    ) {
        try {
            if (!gateway) {
                throw gateway::InternalServerException(protocol_name + " Gateway not enabled");
            }
            
            json body;
            try {
                body = json::parse(req.body);
            } catch (const json::exception& e) {
                throw gateway::JsonParseException(e.what());
            }

            TDevice device;
            try {
                device = parse_func(body);
            } catch (const std::exception& e) {
                 // Convert generic parsing errors to Bad Request
                 throw gateway::BadRequestException(e.what());
            }
            
            std::string device_id = add_func(gateway.get(), device);
            
            api::utils::ResponseHelper::Success(res, response_func(device_id, device), "Device added successfully");
        } catch (const gateway::GatewayException& e) {
            ::utils::Logger::Get()->error("[DeviceHandler][{}] Add device failed: {}", protocol_name, e.what());
            api::utils::ResponseHelper::Error(res, e.code(), e.what(), 200);
        } catch (const std::exception& e) {
            ::utils::Logger::Get()->error("[DeviceHandler][{}] Add device failed: {}", protocol_name, e.what());
            api::utils::ResponseHelper::Error(res, e);
        }
    }

    // ... HandleDelete ...
    template<typename TGateway>
    static void HandleDelete(
        std::shared_ptr<TGateway> gateway,
        const std::string& protocol_name,
        const std::string& device_id,
        httplib::Response& res,
        std::function<bool(TGateway*, const std::string&)> remove_func
    ) {
        try {
            if (!gateway) {
                throw gateway::InternalServerException(protocol_name + " Gateway not enabled");
            }
            
            remove_func(gateway.get(), device_id);
            
            json data = {
                {"device_id", device_id}
            };
            api::utils::ResponseHelper::Success(res, data, "Device deleted successfully");
        } catch (const gateway::GatewayException& e) {
            ::utils::Logger::Get()->error("[DeviceHandler][{}] Delete device failed: {}", protocol_name, e.what());
            api::utils::ResponseHelper::Error(res, e.code(), e.what(), 200);
        } catch (const std::exception& e) {
            ::utils::Logger::Get()->error("[DeviceHandler][{}] Delete device failed: {}", protocol_name, e.what());
            api::utils::ResponseHelper::Error(res, e);
        }
    }

    // ... HandleGetRTSPURLs ...
    template<typename TGateway>
    static void HandleGetRTSPURLs(
        std::shared_ptr<TGateway> gateway,
        const std::string& protocol_name,
        const std::string& device_id,
        httplib::Response& res,
        std::function<std::vector<std::string>(TGateway*, const std::string&)> get_urls_func
    ) {
        try {
            if (!gateway) {
                throw gateway::InternalServerException(protocol_name + " Gateway not enabled");
            }
            
            auto rtsp_urls = get_urls_func(gateway.get(), device_id);
            
            json response_data = json::array();
            for (const auto& url : rtsp_urls) {
                json stream_json;
                stream_json["stream_url"] = url;
                response_data.push_back(stream_json);
            }
            
            api::utils::ResponseHelper::Success(res, response_data, "success");
        } catch (const gateway::GatewayException& e) {
            ::utils::Logger::Get()->error("[DeviceHandler][{}] Get device RTSP URLs failed: {}", protocol_name, e.what());
            api::utils::ResponseHelper::Error(res, e.code(), e.what(), 200);
        } catch (const std::exception& e) {
            ::utils::Logger::Get()->error("[DeviceHandler][{}] Get device RTSP URLs failed: {}", protocol_name, e.what());
            api::utils::ResponseHelper::Error(res, e);
        }
    }
};

} // namespace handlers
} // namespace api

#endif // API_HANDLERS_DEVICE_HANDLER_HELPER_HPP
