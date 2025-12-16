#ifndef GATEWAY_LOCAL_CAMERA_DEVICE_MANAGER_HPP
#define GATEWAY_LOCAL_CAMERA_DEVICE_MANAGER_HPP

#include <string>
#include <vector>
#include <map>
#include <mutex>
#include <memory>
#include <chrono>
#include <set>

// 前向声明
namespace utils {
class CameraDetector;
struct CameraDevice;
struct AudioDevice;
}

namespace api {
class WebSocketServer;
}

namespace config {
struct Config;
}

// LocalCameraDevice 定义在单独的头文件中，避免循环依赖
#include "gateway/local_camera/local_camera_device.hpp"

namespace gateway {
namespace local_camera {

/**
 * @brief 设备管理器
 * 
 * 负责管理本地摄像头设备的发现、缓存、查询等功能
 */
class DeviceManager {
public:
    /**
     * @brief 构造函数
     * @param config 配置对象
     * @param camera_detector 摄像头检测器
     * @param websocket_server WebSocket 服务器（可选，用于推送设备更新）
     */
    DeviceManager(std::shared_ptr<config::Config> config,
                  std::unique_ptr<::utils::CameraDetector>& camera_detector,
                  std::shared_ptr<api::WebSocketServer> websocket_server = nullptr);

    /**
     * @brief 发现本地摄像头设备
     * @return 摄像头设备列表
     */
    std::vector<LocalCameraDevice> DiscoverCameras();

    /**
     * @brief 获取所有已发现的摄像头设备（会重新扫描并更新设备列表）
     * @param refresh 是否重新扫描设备（默认true）
     * @return 摄像头设备列表
     */
    std::vector<LocalCameraDevice> ListCameras(bool refresh = true);

    /**
     * @brief 获取指定设备ID的摄像头信息
     * @param device_id 设备ID
     * @return 摄像头设备信息，不存在返回空对象（device_id为空）
     */
    LocalCameraDevice GetCamera(const std::string& device_id) const;

    /**
     * @brief 从设备ID获取摄像头索引
     * @param device_id 设备ID
     * @return 摄像头索引，失败返回-1
     */
    int GetCameraIndexFromDeviceID(const std::string& device_id) const;

    /**
     * @brief 查询设备能力（支持的分辨率和帧率）
     * @param device_id 设备ID
     * @return 设备能力信息（分辨率列表和帧率列表）
     */
    std::pair<std::vector<std::string>, std::vector<int>> QueryDeviceCapabilities(const std::string& device_id) const;

private:
    /**
     * @brief 检查缓存是否过期
     * @return 是否过期
     */
    bool IsCacheExpired() const;

    /**
     * @brief 更新设备列表（检测新增和移除的设备）
     * @param devices 新发现的设备列表
     */
    void UpdateDeviceList(const std::vector<::utils::CameraDevice>& devices);

    /**
     * @brief 通过 WebSocket 推送设备更新
     * @param device 设备信息
     * @param action 操作类型（"added" 或 "removed"）
     */
    void BroadcastDeviceUpdate(const gateway::LocalCameraDevice& device, const std::string& action) const;

    std::shared_ptr<config::Config> config_;
    std::unique_ptr<::utils::CameraDetector>& camera_detector_;
    std::shared_ptr<api::WebSocketServer> websocket_server_;

    // 设备管理
    std::map<std::string, gateway::LocalCameraDevice> cameras_;  // device_id -> LocalCameraDevice
    mutable std::mutex cameras_mutex_;
    std::chrono::steady_clock::time_point last_discovery_time_;  // 上次设备发现时间

    // 设备能力查询缓存
    struct CapabilityCacheEntry {
        std::vector<std::string> resolutions;
        std::vector<int> fps;
        std::chrono::steady_clock::time_point cache_time;
    };
    mutable std::map<std::string, CapabilityCacheEntry> capability_cache_;
    mutable std::mutex capability_cache_mutex_;
    int capability_cache_ttl_seconds_ = 300;  // 缓存 5 分钟
};

} // namespace local_camera
} // namespace gateway

#endif // GATEWAY_LOCAL_CAMERA_DEVICE_MANAGER_HPP

