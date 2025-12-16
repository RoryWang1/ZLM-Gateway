#include "gateway/local_camera/device_resolver.hpp"
#include "gateway/local_camera/device_manager.hpp"
#include "utils/logger.hpp"
#include <sstream>

namespace gateway {
namespace local_camera {

DeviceResolver::DeviceResolver(DeviceManager* device_manager)
    : device_manager_(device_manager) {
}

DeviceResolver::ResolveResult DeviceResolver::ResolveDevice(const std::string& source_url) {
    ResolveResult result;

    // 首先尝试直接作为 device_id 查找（支持前端直接传递 device_id）
    if (source_url.find("local-camera-") == 0) {
        // 纯 device_id 格式（如: local-camera-2fc991225ea53bce）
        return FindDeviceById(source_url);
    }

    // 解析 source_url 格式（如: local-camera://device_id:xxx 或 local-camera://0）
    std::string device_id;
    int camera_index = -1;
    bool use_device_id = ParseSourceUrl(source_url, device_id, camera_index);

    if (!use_device_id && camera_index < 0) {
        result.success = false;
        result.error_message = "无效的source_url格式: " + source_url;
        return result;
    }

    // 根据格式查找设备
    if (use_device_id) {
        return FindDeviceById(device_id);
    } else {
        return FindDeviceByIndex(camera_index);
    }
}

bool DeviceResolver::ParseSourceUrl(const std::string& source_url, std::string& device_id, int& camera_index) const {
    if (source_url.find("local-camera://device_id:") == 0) {
        // 新格式：使用 device_id
        device_id = source_url.substr(25);  // 跳过 "local-camera://device_id:" (25个字符)
        LOG_DEBUG("[DeviceResolver] 使用 device_id 格式: {}", device_id);
        return true;
    } else if (source_url.find("local-camera://") == 0) {
        // 旧格式：使用索引（兼容性支持）
        try {
            std::string index_str = source_url.substr(15);  // 跳过 "local-camera://"
            camera_index = std::stoi(index_str);
            LOG_DEBUG("[DeviceResolver] 使用索引格式: {}", camera_index);
            return false;
        } catch (const std::exception& e) {
            LOG_ERROR("[DeviceResolver] 解析摄像头索引失败: {}", e.what());
            camera_index = -1;
            return false;
        }
    } else {
        LOG_ERROR("[DeviceResolver] 无效的source_url格式: {}", source_url);
        camera_index = -1;
        return false;
    }
}

DeviceResolver::ResolveResult DeviceResolver::FindDeviceById(const std::string& device_id) {
    ResolveResult result;

    // 先尝试从缓存中获取
    LocalCameraDevice camera = device_manager_->GetCamera(device_id);
    if (camera.device_id.empty()) {
        // 缓存中没有，尝试刷新一次
        LOG_DEBUG("[DeviceResolver] 设备 {} 不在缓存中，尝试刷新设备列表...", device_id);
        device_manager_->DiscoverCameras();
        camera = device_manager_->GetCamera(device_id);
        if (camera.device_id.empty()) {
            result.success = false;
            result.error_message = "设备不存在: device_id=" + device_id;
            return result;
        }
    }

    result.success = true;
    result.device_id = camera.device_id;
    result.camera_index = camera.index;
    result.camera = camera;
    LOG_INFO("[DeviceResolver] 通过 device_id 找到设备: {} (名称: {}, 索引: {})",
            device_id, camera.name, camera.index);
    return result;
}

DeviceResolver::ResolveResult DeviceResolver::FindDeviceByIndex(int camera_index) {
    ResolveResult result;

    // 先尝试从缓存中获取
    auto cameras = device_manager_->ListCameras(false);
    bool camera_exists = false;
    for (const auto& cam : cameras) {
        if (cam.index == camera_index) {
            camera_exists = true;
            result.device_id = cam.device_id;
            result.camera_index = cam.index;
            result.camera = cam;
            break;
        }
    }

    if (!camera_exists) {
        // 尝试重新发现设备
        LOG_INFO("[DeviceResolver] 摄像头索引 {} 不在已发现列表中，尝试重新发现设备...", camera_index);
        device_manager_->DiscoverCameras();
        cameras = device_manager_->ListCameras(true);
        for (const auto& cam : cameras) {
            if (cam.index == camera_index) {
                camera_exists = true;
                result.device_id = cam.device_id;
                result.camera_index = cam.index;
                result.camera = cam;
                LOG_INFO("[DeviceResolver] 重新发现设备成功: index={}, device_id={}", camera_index, result.device_id);
                break;
            }
        }
    }

    if (!camera_exists) {
        result.success = false;
        std::ostringstream oss;
        oss << "摄像头不存在: index=" << camera_index << "，已发现的设备索引: ";
        if (cameras.empty()) {
            oss << "无";
        } else {
            for (size_t i = 0; i < cameras.size(); ++i) {
                if (i > 0) oss << ", ";
                oss << cameras[i].index;
            }
        }
        result.error_message = oss.str();
        return result;
    }

    result.success = true;
    return result;
}

} // namespace local_camera
} // namespace gateway

