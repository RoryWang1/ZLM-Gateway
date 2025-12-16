#ifndef GATEWAY_LOCAL_CAMERA_DEVICE_RESOLVER_HPP
#define GATEWAY_LOCAL_CAMERA_DEVICE_RESOLVER_HPP

#include "gateway/local_camera/local_camera_device.hpp"
#include <string>
#include <memory>

namespace gateway {
namespace local_camera {

class DeviceManager;

/**
 * @brief 设备解析器
 * 
 * 负责从 source_url 解析设备信息，并查找对应的设备
 */
class DeviceResolver {
public:
    /**
     * @brief 解析结果
     */
    struct ResolveResult {
        bool success = false;
        std::string device_id;
        int camera_index = -1;
        LocalCameraDevice camera;
        std::string error_message;
    };

    /**
     * @brief 构造函数
     * @param device_manager 设备管理器
     */
    DeviceResolver(DeviceManager* device_manager);

    /**
     * @brief 解析 source_url 并查找设备
     * @param source_url 源URL（支持两种格式：local-camera://device_id:{id} 或 local-camera://{index}）
     * @return 解析结果
     */
    ResolveResult ResolveDevice(const std::string& source_url);

private:
    /**
     * @brief 解析 source_url 格式
     * @param source_url 源URL
     * @param device_id 输出的设备ID（如果使用 device_id 格式）
     * @param camera_index 输出的摄像头索引（如果使用索引格式）
     * @return 是否使用 device_id 格式（true=使用device_id，false=使用索引）
     */
    bool ParseSourceUrl(const std::string& source_url, std::string& device_id, int& camera_index) const;

    /**
     * @brief 通过 device_id 查找设备
     * @param device_id 设备ID
     * @return 解析结果
     */
    ResolveResult FindDeviceById(const std::string& device_id);

    /**
     * @brief 通过索引查找设备
     * @param camera_index 摄像头索引
     * @return 解析结果
     */
    ResolveResult FindDeviceByIndex(int camera_index);

    DeviceManager* device_manager_;
};

} // namespace local_camera
} // namespace gateway

#endif // GATEWAY_LOCAL_CAMERA_DEVICE_RESOLVER_HPP

