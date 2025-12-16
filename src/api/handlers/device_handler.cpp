#include "api/handlers/device_handler.hpp"
#include "api/handlers/device_handler_helper.hpp"
#include "api/utils/response_helper.hpp"
#include "gateway/onvif/onvif_gateway.hpp"
#include "gateway/isapi/isapi_gateway.hpp"
#include "gateway/dahua/dahua_gateway.hpp"
#include "gateway/psia/psia_gateway.hpp"
#include "gateway/gb28181/gb28181_gateway.hpp"
#include "gateway/local_camera/local_camera_gateway.hpp"
#include "streaming/stream_manager.hpp"
#include "utils/logger.hpp"
#include <nlohmann/json.hpp>
#include <chrono>

using json = nlohmann::json;

namespace api {
namespace handlers {


DeviceHandler::DeviceHandler(
    const gateway::utils::GatewayFactory::GatewayInstances& gateways,
    std::shared_ptr<streaming::StreamManager> stream_manager) 
    : stream_manager_(stream_manager) {
    
    // Initialize specific members via generic retrieval
    onvif_gateway_ = gateways.Get<gateway::ONVIFGateway>("onvif");
    isapi_gateway_ = gateways.Get<gateway::ISAPIGateway>("isapi");
    dahua_gateway_ = gateways.Get<gateway::DahuaGateway>("dahua");
    psia_gateway_ = gateways.Get<gateway::PSIAGateway>("psia");
    gb28181_gateway_ = gateways.Get<gateway::GB28181Gateway>("gb28181");
    local_camera_gateway_ = gateways.Get<gateway::LocalCameraGateway>("local_camera");
}



// Helper function to convert ONVIFDevice to JSON
static json ONVIFDeviceToJson(const gateway::ONVIFDevice& device) {
    json device_json;
    device_json["id"] = device.id;
    device_json["xaddr"] = device.xaddr;
    device_json["types"] = device.types;
    device_json["scopes"] = device.scopes;
    device_json["manufacturer"] = device.manufacturer;
    device_json["model"] = device.model;
    device_json["serial_number"] = device.serial_number;
    device_json["hardware_id"] = device.hardware_id;
    device_json["has_credentials"] = !device.username.empty();
    
    auto time_since_epoch = std::chrono::duration_cast<std::chrono::seconds>(
        device.last_seen.time_since_epoch()).count();
    device_json["last_seen"] = time_since_epoch;
    return device_json;
}

// ONVIF 设备管理（直接调用Gateway）
void DeviceHandler::HandleDiscoverDevices(const httplib::Request& req, httplib::Response& res) {
    DeviceHandlerHelper::HandleDiscover<gateway::ONVIFGateway, gateway::ONVIFDevice>(
        onvif_gateway_, "ONVIF", req, res,
        [](gateway::ONVIFGateway* gateway, const httplib::Request& r) {
            int timeout_seconds = 5;
            if (!r.body.empty()) {
                try {
                    auto body = json::parse(r.body);
                    timeout_seconds = body.value("timeout_seconds", 5);
                } catch (...) {}
            }
            return gateway->DiscoverDevices(timeout_seconds);
        },
        ONVIFDeviceToJson
    );
}

void DeviceHandler::HandleGetDevices(const httplib::Request& /* req */, httplib::Response& res) {
    DeviceHandlerHelper::HandleList<gateway::ONVIFGateway, gateway::ONVIFDevice>(
        onvif_gateway_, "ONVIF", res,
        [](gateway::ONVIFGateway* gateway) {
            return gateway->ListDevices();
        },
        ONVIFDeviceToJson
    );
}

// Helper to get ID from request (supports both :id pattern and regex)
static std::string GetIdFromRequest(const httplib::Request& req) {
    if (req.path_params.count("id")) return req.path_params.at("id");
    if (req.path_params.count("device_id")) return req.path_params.at("device_id");
    if (req.matches.size() > 1) return req.matches[1].str();
    return "";
}

void DeviceHandler::HandleGetDevice(const httplib::Request& req, httplib::Response& res) {
    std::string device_id = GetIdFromRequest(req);
    DeviceHandlerHelper::HandleGet<gateway::ONVIFGateway, gateway::ONVIFDevice>(
        onvif_gateway_, "ONVIF", device_id, res,
        [](gateway::ONVIFGateway* gateway, const std::string& id) {
            return gateway->GetDevice(id);
        },
        ONVIFDeviceToJson,
        [](const gateway::ONVIFDevice& device) {
            return !device.id.empty();
        }
    );
}

void DeviceHandler::HandleGetDeviceRTSPURLs(const httplib::Request& req, httplib::Response& res) {
    std::string device_id = GetIdFromRequest(req);
    DeviceHandlerHelper::HandleGetRTSPURLs<gateway::ONVIFGateway>(
        onvif_gateway_, "ONVIF", device_id, res,
        [](gateway::ONVIFGateway* gateway, const std::string& id) {
            return gateway->GetDeviceRTSPURLs(id);
        }
    );
}

void DeviceHandler::HandleDeleteDevice(const httplib::Request& req, httplib::Response& res) {
    std::string device_id = GetIdFromRequest(req);
    DeviceHandlerHelper::HandleDelete<gateway::ONVIFGateway>(
        onvif_gateway_, "ONVIF", device_id, res,
        [](gateway::ONVIFGateway* gateway, const std::string& id) {
            return gateway->RemoveDevice(id);
        }
    );
}

void DeviceHandler::HandleAddDevice(const httplib::Request& req, httplib::Response& res) {
    DeviceHandlerHelper::HandleAdd<gateway::ONVIFGateway, gateway::ONVIFDevice>(
        onvif_gateway_, "ONVIF", req, res,
        [](const json& body) {
            gateway::ONVIFDevice device;
            if (body.contains("xaddr")) {
                device.xaddr = body["xaddr"].get<std::string>();
            } else {
                throw std::runtime_error("Missing required field: xaddr");
            }
            if (body.contains("username")) device.username = body["username"].get<std::string>();
            if (body.contains("password")) device.password = body["password"].get<std::string>();
            return device;
        },
        [](gateway::ONVIFGateway* gateway, const gateway::ONVIFDevice& device) {
            return gateway->AddDevice(device);
        },
        [](const std::string& id, const gateway::ONVIFDevice& device) {
            return json{{"device_id", id}, {"xaddr", device.xaddr}};
        }
    );
}

// Helper function to convert ISAPIDevice to JSON
static json ISAPIDeviceToJson(const gateway::ISAPIDevice& device) {
    json device_json;
    device_json["id"] = device.id;
    device_json["base_url"] = device.base_url;
    device_json["manufacturer"] = device.manufacturer;
    device_json["model"] = device.model;
    device_json["serial_number"] = device.serial_number;
    device_json["firmware_version"] = device.firmware_version;
    device_json["has_credentials"] = !device.username.empty();
    
    auto time_since_epoch = std::chrono::duration_cast<std::chrono::seconds>(
        device.last_seen.time_since_epoch()).count();
    device_json["last_seen"] = time_since_epoch;
    return device_json;
}

// ISAPI 设备管理（直接调用Gateway）
void DeviceHandler::HandleDiscoverISAPIDevices(const httplib::Request& req, httplib::Response& res) {
    DeviceHandlerHelper::HandleDiscover<gateway::ISAPIGateway, gateway::ISAPIDevice>(
        isapi_gateway_, "ISAPI", req, res,
        [](gateway::ISAPIGateway* gateway, const httplib::Request& r) {
            std::string ip_range = "";
            int port = 80;
            if (!r.body.empty()) {
                try {
                    auto body = json::parse(r.body);
                    ip_range = body.value("ip_range", "192.168.1.0/24");
                    port = body.value("port", 80);
                } catch (...) {}
            }
            return gateway->DiscoverDevices(ip_range, port, 5);
        },
        ISAPIDeviceToJson
    );
}

void DeviceHandler::HandleGetISAPIDevices(const httplib::Request&, httplib::Response& res) {
    DeviceHandlerHelper::HandleList<gateway::ISAPIGateway, gateway::ISAPIDevice>(
        isapi_gateway_, "ISAPI", res,
        [](gateway::ISAPIGateway* gateway) {
            return gateway->ListDevices();
        },
        ISAPIDeviceToJson
    );
}

void DeviceHandler::HandleGetISAPIDevice(const httplib::Request& req, httplib::Response& res) {
    std::string device_id = GetIdFromRequest(req);
    DeviceHandlerHelper::HandleGet<gateway::ISAPIGateway, gateway::ISAPIDevice>(
        isapi_gateway_, "ISAPI", device_id, res,
        [](gateway::ISAPIGateway* gateway, const std::string& id) {
            return gateway->GetDevice(id);
        },
        ISAPIDeviceToJson,
        [](const gateway::ISAPIDevice& device) {
            return !device.id.empty();
        }
    );
}

void DeviceHandler::HandleGetISAPIDeviceRTSPURLs(const httplib::Request& req, httplib::Response& res) {
    std::string device_id = GetIdFromRequest(req);
    DeviceHandlerHelper::HandleGetRTSPURLs<gateway::ISAPIGateway>(
        isapi_gateway_, "ISAPI", device_id, res,
        [](gateway::ISAPIGateway* gateway, const std::string& id) {
            return gateway->GetDeviceRTSPURLs(id);
        }
    );
}

void DeviceHandler::HandleAddISAPIDevice(const httplib::Request& req, httplib::Response& res) {
    DeviceHandlerHelper::HandleAdd<gateway::ISAPIGateway, gateway::ISAPIDevice>(
        isapi_gateway_, "ISAPI", req, res,
        [](const json& body) {
            gateway::ISAPIDevice device;
            if (body.contains("base_url")) {
                device.base_url = body["base_url"].get<std::string>();
            } else {
                throw std::runtime_error("Missing required field: base_url");
            }
            if (body.contains("username")) device.username = body["username"].get<std::string>();
            if (body.contains("password")) device.password = body["password"].get<std::string>();
            return device;
        },
        [](gateway::ISAPIGateway* gateway, const gateway::ISAPIDevice& device) {
            return gateway->AddDevice(device);
        },
        [](const std::string& id, const gateway::ISAPIDevice& device) {
            return json{{"device_id", id}, {"base_url", device.base_url}};
        }
    );
}

void DeviceHandler::HandleDeleteISAPIDevice(const httplib::Request& req, httplib::Response& res) {
    std::string device_id = GetIdFromRequest(req);
    DeviceHandlerHelper::HandleDelete<gateway::ISAPIGateway>(
        isapi_gateway_, "ISAPI", device_id, res,
        [](gateway::ISAPIGateway* gateway, const std::string& id) {
            return gateway->RemoveDevice(id);
        }
    );
}
// Helper function to convert DahuaDevice to JSON
static json DahuaDeviceToJson(const gateway::DahuaDevice& device) {
    json device_json;
    device_json["id"] = device.id;
    device_json["base_url"] = device.base_url;
    device_json["manufacturer"] = device.manufacturer;
    device_json["model"] = device.model;
    device_json["serial_number"] = device.serial_number;
    device_json["firmware_version"] = device.firmware_version;
    device_json["has_credentials"] = !device.username.empty();
    
    auto time_since_epoch = std::chrono::duration_cast<std::chrono::seconds>(
        device.last_seen.time_since_epoch()).count();
    device_json["last_seen"] = time_since_epoch;
    return device_json;
}

// Dahua 设备管理（直接调用Gateway）
void DeviceHandler::HandleDiscoverDahuaDevices(const httplib::Request& req, httplib::Response& res) {
    DeviceHandlerHelper::HandleDiscover<gateway::DahuaGateway, gateway::DahuaDevice>(
        dahua_gateway_, "Dahua", req, res,
        [](gateway::DahuaGateway* gateway, const httplib::Request& r) {
            std::string ip_range = "";
            int port = 80;
            if (!r.body.empty()) {
                try {
                    auto body = json::parse(r.body);
                    ip_range = body.value("ip_range", "");
                    port = body.value("port", 80);
                } catch (...) {}
            }
            return gateway->DiscoverDevices(ip_range, port, 5);
        },
        DahuaDeviceToJson
    );
}

void DeviceHandler::HandleGetDahuaDevices(const httplib::Request&, httplib::Response& res) {
    DeviceHandlerHelper::HandleList<gateway::DahuaGateway, gateway::DahuaDevice>(
        dahua_gateway_, "Dahua", res,
        [](gateway::DahuaGateway* gateway) {
            return gateway->ListDevices();
        },
        DahuaDeviceToJson
    );
}

void DeviceHandler::HandleGetDahuaDevice(const httplib::Request& req, httplib::Response& res) {
    std::string device_id = GetIdFromRequest(req);
    DeviceHandlerHelper::HandleGet<gateway::DahuaGateway, gateway::DahuaDevice>(
        dahua_gateway_, "Dahua", device_id, res,
        [](gateway::DahuaGateway* gateway, const std::string& id) {
            return gateway->GetDevice(id);
        },
        DahuaDeviceToJson,
        [](const gateway::DahuaDevice& device) {
            return !device.id.empty();
        }
    );
}

void DeviceHandler::HandleGetDahuaDeviceRTSPURLs(const httplib::Request& req, httplib::Response& res) {
    std::string device_id = GetIdFromRequest(req);
    DeviceHandlerHelper::HandleGetRTSPURLs<gateway::DahuaGateway>(
        dahua_gateway_, "Dahua", device_id, res,
        [](gateway::DahuaGateway* gateway, const std::string& id) {
            return gateway->GetDeviceRTSPURLs(id);
        }
    );
}

void DeviceHandler::HandleAddDahuaDevice(const httplib::Request& req, httplib::Response& res) {
    DeviceHandlerHelper::HandleAdd<gateway::DahuaGateway, gateway::DahuaDevice>(
        dahua_gateway_, "Dahua", req, res,
        [](const json& body) {
            gateway::DahuaDevice device;
            if (body.contains("base_url")) {
                device.base_url = body["base_url"].get<std::string>();
            } else {
                throw std::runtime_error("Missing required field: base_url");
            }
            if (body.contains("username")) device.username = body["username"].get<std::string>();
            if (body.contains("password")) device.password = body["password"].get<std::string>();
            return device;
        },
        [](gateway::DahuaGateway* gateway, const gateway::DahuaDevice& device) {
            return gateway->AddDevice(device);
        },
        [](const std::string& id, const gateway::DahuaDevice& device) {
            return json{{"device_id", id}, {"base_url", device.base_url}};
        }
    );
}
void DeviceHandler::HandleDeleteDahuaDevice(const httplib::Request& req, httplib::Response& res) {
    std::string device_id = GetIdFromRequest(req);
    DeviceHandlerHelper::HandleDelete<gateway::DahuaGateway>(
        dahua_gateway_, "Dahua", device_id, res,
        [](gateway::DahuaGateway* gateway, const std::string& id) {
            return gateway->RemoveDevice(id);
        }
    );
}

// Helper function to convert PSIADevice to JSON
static json PSIADeviceToJson(const gateway::PSIADevice& device) {
    json device_json;
    device_json["id"] = device.id;
    device_json["base_url"] = device.base_url;
    device_json["manufacturer"] = device.manufacturer;
    device_json["model"] = device.model;
    device_json["serial_number"] = device.serial_number;
    device_json["firmware_version"] = device.firmware_version;
    device_json["has_credentials"] = !device.username.empty();
    
    auto time_since_epoch = std::chrono::duration_cast<std::chrono::seconds>(
        device.last_seen.time_since_epoch()).count();
    device_json["last_seen"] = time_since_epoch;
    return device_json;
}

// PSIA 设备管理（直接调用Gateway）
void DeviceHandler::HandleDiscoverPSIADevices(const httplib::Request& req, httplib::Response& res) {
    DeviceHandlerHelper::HandleDiscover<gateway::PSIAGateway, gateway::PSIADevice>(
        psia_gateway_, "PSIA", req, res,
        [](gateway::PSIAGateway* gateway, const httplib::Request& r) {
            std::string ip_range = "";
            int port = 80;
            if (!r.body.empty()) {
                try {
                    auto body = json::parse(r.body);
                    ip_range = body.value("ip_range", "");
                    port = body.value("port", 80);
                } catch (...) {}
            }
            return gateway->DiscoverDevices(ip_range, port, 5);
        },
        PSIADeviceToJson
    );
}

void DeviceHandler::HandleGetPSIADevices(const httplib::Request&, httplib::Response& res) {
    DeviceHandlerHelper::HandleList<gateway::PSIAGateway, gateway::PSIADevice>(
        psia_gateway_, "PSIA", res,
        [](gateway::PSIAGateway* gateway) {
            return gateway->ListDevices();
        },
        PSIADeviceToJson
    );
}

void DeviceHandler::HandleGetPSIADevice(const httplib::Request& req, httplib::Response& res) {
    std::string device_id = GetIdFromRequest(req);
    DeviceHandlerHelper::HandleGet<gateway::PSIAGateway, gateway::PSIADevice>(
        psia_gateway_, "PSIA", device_id, res,
        [](gateway::PSIAGateway* gateway, const std::string& id) {
            return gateway->GetDevice(id);
        },
        PSIADeviceToJson,
        [](const gateway::PSIADevice& device) {
            return !device.id.empty();
        }
    );
}

void DeviceHandler::HandleGetPSIADeviceRTSPURLs(const httplib::Request& req, httplib::Response& res) {
    std::string device_id = GetIdFromRequest(req);
    DeviceHandlerHelper::HandleGetRTSPURLs<gateway::PSIAGateway>(
        psia_gateway_, "PSIA", device_id, res,
        [](gateway::PSIAGateway* gateway, const std::string& id) {
            return gateway->GetDeviceRTSPURLs(id);
        }
    );
}

void DeviceHandler::HandleAddPSIADevice(const httplib::Request& req, httplib::Response& res) {
    DeviceHandlerHelper::HandleAdd<gateway::PSIAGateway, gateway::PSIADevice>(
        psia_gateway_, "PSIA", req, res,
        [](const json& body) {
            gateway::PSIADevice device;
            if (body.contains("base_url")) {
                device.base_url = body["base_url"].get<std::string>();
            } else {
                throw std::runtime_error("Missing required field: base_url");
            }
            if (body.contains("username")) device.username = body["username"].get<std::string>();
            if (body.contains("password")) device.password = body["password"].get<std::string>();
            return device;
        },
        [](gateway::PSIAGateway* gateway, const gateway::PSIADevice& device) {
            return gateway->AddDevice(device);
        },
        [](const std::string& id, const gateway::PSIADevice& device) {
            return json{{"device_id", id}, {"base_url", device.base_url}};
        }
    );
}

void DeviceHandler::HandleDeletePSIADevice(const httplib::Request& req, httplib::Response& res) {
    std::string device_id = GetIdFromRequest(req);
    DeviceHandlerHelper::HandleDelete<gateway::PSIAGateway>(
        psia_gateway_, "PSIA", device_id, res,
        [](gateway::PSIAGateway* gateway, const std::string& id) {
            return gateway->RemoveDevice(id);
        }
    );
}

// GB28181 设备管理（使用适配器）
void DeviceHandler::HandleGB28181GetDevices(const httplib::Request& /* req */, httplib::Response& res) {
    try {
        if (!gb28181_gateway_) {
            api::utils::ResponseHelper::Error(res, -1, "GB28181 Gateway not enabled", 500);
            return;
        }

        auto devices = gb28181_gateway_->ListDevices();

        json response_data = {
            {"devices", json::array()},
            {"count", devices.size()}
        };

        for (const auto& device : devices) {
            json device_json;
            device_json["id"] = device.id;
            device_json["name"] = device.name;
            device_json["ip"] = device.ip;
            device_json["port"] = device.port;
            response_data["devices"].push_back(device_json);
        }

        api::utils::ResponseHelper::Success(res, response_data, "success");
    } catch (const std::exception& e) {
        ::utils::Logger::Get()->error("Get GB28181 devices failed: {}", e.what());
        api::utils::ResponseHelper::Error(res, e);
    }
}

void DeviceHandler::HandleGB28181GetDevice(const httplib::Request& req, httplib::Response& res) {
    try {
        if (!gb28181_gateway_) {
            api::utils::ResponseHelper::Error(res, -1, "GB28181 Gateway not enabled", 500);
            return;
        }

        std::string device_id = req.matches[1];
        auto device = gb28181_gateway_->GetDevice(device_id);

        if (device.id.empty()) {
            api::utils::ResponseHelper::NotFound(res, "Device not found");
            return;
        }

        json device_json;
        device_json["id"] = device.id;
        device_json["name"] = device.name;
        device_json["ip"] = device.ip;
        device_json["port"] = device.port;

        api::utils::ResponseHelper::Success(res, device_json, "success");
    } catch (const std::exception& e) {
        ::utils::Logger::Get()->error("Get GB28181 device failed: {}", e.what());
        api::utils::ResponseHelper::Error(res, e);
    }
}

void DeviceHandler::HandleGB28181AddDevice(const httplib::Request& req, httplib::Response& res) {
    try {
        if (!gb28181_gateway_) {
            api::utils::ResponseHelper::Error(res, -1, "GB28181 Gateway not enabled", 500);
            return;
        }

        auto body = json::parse(req.body);
        gateway::GB28181Device device;
        
        if (body.contains("id")) device.id = body["id"].get<std::string>();
        if (body.contains("name")) device.name = body["name"].get<std::string>();
        if (body.contains("ip")) device.ip = body["ip"].get<std::string>();
        if (body.contains("port")) device.port = body["port"].get<int>();

        std::string device_id = gb28181_gateway_->AddDevice(device);
        
        json response_data = {
            {"device_id", device_id}
        };
        api::utils::ResponseHelper::Success(res, response_data, "Device added successfully");
    } catch (const json::exception& e) {
        ::utils::Logger::Get()->error("Parse request body failed: {}", e.what());
        api::utils::ResponseHelper::BadRequest(res, "Invalid request body");
    } catch (const std::exception& e) {
        ::utils::Logger::Get()->error("Add GB28181 device failed: {}", e.what());
        api::utils::ResponseHelper::Error(res, e);
    }
}

void DeviceHandler::HandleGB28181DeleteDevice(const httplib::Request& req, httplib::Response& res) {
    try {
        if (!gb28181_gateway_) {
            api::utils::ResponseHelper::Error(res, -1, "GB28181 Gateway not enabled", 500);
            return;
        }

        std::string device_id = req.matches[1];
        gb28181_gateway_->RemoveDevice(device_id);

        json data = {
            {"device_id", device_id}
        };
        api::utils::ResponseHelper::Success(res, data, "Device deleted successfully");
    } catch (const std::exception& e) {
        ::utils::Logger::Get()->error("Delete GB28181 device failed: {}", e.what());
        api::utils::ResponseHelper::Error(res, e);
    }
}

// ===== LocalCamera Device Management =====

// Helper function to convert LocalCameraDevice to JSON
static json LocalCameraDeviceToJson(const gateway::LocalCameraDevice& device) {
    json device_json;
    device_json["device_id"] = device.device_id;
    device_json["name"] = device.name;
    device_json["protocol"] = "local-camera";
    device_json["index"] = device.index;
    device_json["platform"] = device.platform;
    device_json["device_path"] = device.device_path;
    if (device.audio_device_index >= 0) {
        device_json["audio_device_index"] = device.audio_device_index;
    }
    return device_json;
}

void DeviceHandler::HandleDiscoverLocalCameras(const httplib::Request& req, httplib::Response& res) {
    DeviceHandlerHelper::HandleDiscover<gateway::LocalCameraGateway, gateway::LocalCameraDevice>(
        local_camera_gateway_, "LocalCamera", req, res,
        [](gateway::LocalCameraGateway* gateway, const httplib::Request&) {
            auto result = gateway->DiscoverCameras();
            if (!result.IsSuccess()) throw result.Error();
            return result.Value();
        },
        LocalCameraDeviceToJson
    );
}

void DeviceHandler::HandleGetLocalCameras(const httplib::Request&, httplib::Response& res) {
    DeviceHandlerHelper::HandleList<gateway::LocalCameraGateway, gateway::LocalCameraDevice>(
        local_camera_gateway_, "LocalCamera", res,
        [](gateway::LocalCameraGateway* gateway) {
            auto result = gateway->ListCameras(false);
            if (!result.IsSuccess()) throw result.Error();
            return result.Value();
        },
        LocalCameraDeviceToJson
    );
}

void DeviceHandler::HandleRefreshLocalCameras(const httplib::Request&, httplib::Response& res) {
    DeviceHandlerHelper::HandleList<gateway::LocalCameraGateway, gateway::LocalCameraDevice>(
        local_camera_gateway_, "LocalCamera", res,
        [](gateway::LocalCameraGateway* gateway) {
            auto result = gateway->ListCameras(true);
            if (!result.IsSuccess()) throw result.Error();
            return result.Value();
        },
        LocalCameraDeviceToJson
    );
}

void DeviceHandler::HandleGetLocalCamera(const httplib::Request& req, httplib::Response& res) {
    std::string device_id = req.matches.size() > 1 ? req.matches[1].str() : 
                           (req.path_params.count("id") ? req.path_params.at("id") : "");
    
    // 如果无法从URL获取ID (path_params or matches), Helper 会处理空ID? 
    // DeviceHandlerHelper::HandleGet 接收 device_id 参数。
    
    DeviceHandlerHelper::HandleGet<gateway::LocalCameraGateway, gateway::LocalCameraDevice>(
        local_camera_gateway_, "LocalCamera", device_id, res,
        [](gateway::LocalCameraGateway* gateway, const std::string& id) {
            auto result = gateway->GetCamera(id);
            if (!result.IsSuccess()) throw result.Error();
            return result.Value();
        },
        LocalCameraDeviceToJson,
        [](const gateway::LocalCameraDevice& device) {
            return !device.device_id.empty();
        }
    );
}

void DeviceHandler::HandleGetLocalCameraCapabilities(const httplib::Request& req, httplib::Response& res) {
    try {
        if (!local_camera_gateway_) {
            api::utils::ResponseHelper::Error(res, -1, "Local Camera Gateway not enabled", 500);
            return;
        }
        
        std::string device_id;
        if (req.path_params.find("id") != req.path_params.end()) {
            device_id = req.path_params.at("id");
        } else if (req.matches.size() > 1) {
            device_id = req.matches[1];
        } else {
            api::utils::ResponseHelper::BadRequest(res, "Device ID is required");
            return;
        }
        
        auto result = local_camera_gateway_->QueryDeviceCapabilities(device_id);
        if (!result.IsSuccess()) {
            // Return HTTP 200 with code=-1 for business errors, not HTTP 404
            const auto& error = result.Error();
            api::utils::ResponseHelper::Error(res, error.code(), error.what(), 200);
            return;
        }
        auto capabilities = result.Value();
        
        json data = {
            {"device_id", device_id},
            {"supported_resolutions", capabilities.first},
            {"supported_fps", capabilities.second}
        };
        
        api::utils::ResponseHelper::Success(res, data);
    } catch (const std::exception& e) {
        api::utils::ResponseHelper::Error(res, -1, std::string("Error: ") + e.what(), 500);
    }
}

void DeviceHandler::HandleStartLocalCameraStream(const httplib::Request& req, httplib::Response& res) {
    try {
        if (!local_camera_gateway_) {
            api::utils::ResponseHelper::Error(res, -1, "Local Camera Gateway not enabled", 500);
            return;
        }

        json body = json::parse(req.body);
        
        std::string device_id = body.value("device_id", "");
        std::string app = body.value("app", "live");
        std::string stream = body.value("stream", "");
        std::string output_protocol = body.value("output_protocol", "webrtc");

        if (device_id.empty() || stream.empty()) {
            api::utils::ResponseHelper::BadRequest(res, "device_id and stream cannot be empty");
            return;
        }

        auto result = local_camera_gateway_->StartCameraStream(device_id, app, stream, output_protocol);

        if (result.IsSuccess()) {
            json data = {
                {"app", app},
                {"stream", stream},
                {"device_id", device_id},
                {"output_protocol", output_protocol}
            };
            api::utils::ResponseHelper::Success(res, data, "Local camera stream started successfully");
        } else {
            // Return HTTP 200 with code=-1 for business errors, not HTTP 404
            const auto& error = result.Error();
            api::utils::ResponseHelper::Error(res, error.code(), error.what(), 200);
        }
    } catch (const json::exception& e) {
        ::utils::Logger::Get()->error("Parse request JSON failed: {}", e.what());
        api::utils::ResponseHelper::BadRequest(res, "Invalid JSON format");
    } catch (const std::exception& e) {
        ::utils::Logger::Get()->error("Start local camera stream failed: {}", e.what());
        api::utils::ResponseHelper::Error(res, e);
    }
}

void DeviceHandler::HandleStopLocalCameraStream(const httplib::Request& req, httplib::Response& res) {
    try {
        if (!local_camera_gateway_) {
            api::utils::ResponseHelper::Error(res, -1, "Local Camera Gateway not enabled", 500);
            return;
        }

        if (req.body.empty()) {
            api::utils::ResponseHelper::BadRequest(res, "Request body cannot be empty");
            return;
        }

        json body;
        try {
            body = json::parse(req.body);
        } catch (const json::exception& e) {
            ::utils::Logger::Get()->error("Parse request JSON failed: {}", e.what());
            api::utils::ResponseHelper::BadRequest(res, "Invalid JSON format: " + std::string(e.what()));
            return;
        }
        
        std::string app = body.value("app", "live");
        std::string stream = body.value("stream", "");

        if (stream.empty()) {
            api::utils::ResponseHelper::BadRequest(res, "stream cannot be empty");
            return;
        }

        (void)local_camera_gateway_->StopCameraStream(app, stream);

        try {
            // UnregisterStream 会自动将状态设为 Stopped 并广播更新，不需要单独调用 UpdateStreamStatus
            stream_manager_->UnregisterStream(app, stream);
        } catch (const std::exception& e) {
            ::utils::Logger::Get()->warn("UnregisterStream failed (non-fatal): {}", e.what());
        }

        json data = {
            {"app", app},
            {"stream", stream}
        };
        api::utils::ResponseHelper::Success(res, data, "Local camera stream stopped successfully");
    } catch (const std::exception& e) {
        ::utils::Logger::Get()->error("Stop local camera stream failed: {}", e.what());
        api::utils::ResponseHelper::Error(res, -1, "Internal server error: " + std::string(e.what()), 500);
    } catch (...) {
        ::utils::Logger::Get()->error("Stop local camera stream failed: unknown exception");
        api::utils::ResponseHelper::Error(res, -1, "Internal server error: unknown exception", 500);
    }
}

} // namespace handlers
} // namespace api
