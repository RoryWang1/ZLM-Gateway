#ifndef API_HANDLERS_DEVICE_HANDLER_HPP
#define API_HANDLERS_DEVICE_HANDLER_HPP

#include <httplib.h>
#include <memory>
#include <string>
#include "gateway/utils/gateway_factory.hpp"

namespace gateway {
class ONVIFGateway;
class ISAPIGateway;
class DahuaGateway;
class PSIAGateway;
class GB28181Gateway;
class LocalCameraGateway;
}

namespace streaming {
class StreamManager;
}

namespace api {
namespace handlers {

/**
 * @brief 设备管理请求处理器 - 直接调用Gateway
 */
class DeviceHandler {
public:
    DeviceHandler(
        const gateway::utils::GatewayFactory::GatewayInstances& gateways,
        std::shared_ptr<streaming::StreamManager> stream_manager);

    // 向后兼容的旧接口（保留以支持现有路由）
    // ONVIF 设备管理
    void HandleDiscoverDevices(const httplib::Request& req, httplib::Response& res);
    void HandleGetDevices(const httplib::Request& req, httplib::Response& res);
    void HandleGetDevice(const httplib::Request& req, httplib::Response& res);
    void HandleGetDeviceRTSPURLs(const httplib::Request& req, httplib::Response& res);
    void HandleAddDevice(const httplib::Request& req, httplib::Response& res);
    void HandleDeleteDevice(const httplib::Request& req, httplib::Response& res);

    // ISAPI 设备管理
    void HandleDiscoverISAPIDevices(const httplib::Request& req, httplib::Response& res);
    void HandleGetISAPIDevices(const httplib::Request& req, httplib::Response& res);
    void HandleGetISAPIDevice(const httplib::Request& req, httplib::Response& res);
    void HandleGetISAPIDeviceRTSPURLs(const httplib::Request& req, httplib::Response& res);
    void HandleAddISAPIDevice(const httplib::Request& req, httplib::Response& res);
    void HandleDeleteISAPIDevice(const httplib::Request& req, httplib::Response& res);

    // Dahua 设备管理
    void HandleDiscoverDahuaDevices(const httplib::Request& req, httplib::Response& res);
    void HandleGetDahuaDevices(const httplib::Request& req, httplib::Response& res);
    void HandleGetDahuaDevice(const httplib::Request& req, httplib::Response& res);
    void HandleGetDahuaDeviceRTSPURLs(const httplib::Request& req, httplib::Response& res);
    void HandleAddDahuaDevice(const httplib::Request& req, httplib::Response& res);
    void HandleDeleteDahuaDevice(const httplib::Request& req, httplib::Response& res);

    // PSIA 设备管理
    void HandleDiscoverPSIADevices(const httplib::Request& req, httplib::Response& res);
    void HandleGetPSIADevices(const httplib::Request& req, httplib::Response& res);
    void HandleGetPSIADevice(const httplib::Request& req, httplib::Response& res);
    void HandleGetPSIADeviceRTSPURLs(const httplib::Request& req, httplib::Response& res);
    void HandleAddPSIADevice(const httplib::Request& req, httplib::Response& res);
    void HandleDeletePSIADevice(const httplib::Request& req, httplib::Response& res);

    // GB28181 设备管理
    void HandleGB28181GetDevices(const httplib::Request& req, httplib::Response& res);
    void HandleGB28181GetDevice(const httplib::Request& req, httplib::Response& res);
    void HandleGB28181AddDevice(const httplib::Request& req, httplib::Response& res);
    void HandleGB28181DeleteDevice(const httplib::Request& req, httplib::Response& res);

    // LocalCamera 设备管理
    void HandleDiscoverLocalCameras(const httplib::Request& req, httplib::Response& res);
    void HandleGetLocalCameras(const httplib::Request& req, httplib::Response& res);
    void HandleRefreshLocalCameras(const httplib::Request& req, httplib::Response& res);
    void HandleGetLocalCamera(const httplib::Request& req, httplib::Response& res);
    void HandleGetLocalCameraCapabilities(const httplib::Request& req, httplib::Response& res);
    void HandleStartLocalCameraStream(const httplib::Request& req, httplib::Response& res);
    void HandleStopLocalCameraStream(const httplib::Request& req, httplib::Response& res);

private:

    // Gateway references
    std::shared_ptr<gateway::ONVIFGateway> onvif_gateway_;
    std::shared_ptr<gateway::ISAPIGateway> isapi_gateway_;
    std::shared_ptr<gateway::DahuaGateway> dahua_gateway_;
    std::shared_ptr<gateway::PSIAGateway> psia_gateway_;
    std::shared_ptr<gateway::GB28181Gateway> gb28181_gateway_;
    std::shared_ptr<gateway::LocalCameraGateway> local_camera_gateway_;
    std::shared_ptr<streaming::StreamManager> stream_manager_;
};

} // namespace handlers
} // namespace api

#endif // API_HANDLERS_DEVICE_HANDLER_HPP
