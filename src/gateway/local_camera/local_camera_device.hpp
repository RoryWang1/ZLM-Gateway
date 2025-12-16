#ifndef GATEWAY_LOCAL_CAMERA_LOCAL_CAMERA_DEVICE_HPP
#define GATEWAY_LOCAL_CAMERA_LOCAL_CAMERA_DEVICE_HPP

#include <string>
#include <vector>

namespace gateway {

/**
 * @brief 本地摄像头设备信息
 */
struct LocalCameraDevice {
    std::string device_id;
    std::string name;
    int index;
    std::string platform;
    std::string device_path;
    int audio_device_index = -1;  // 匹配的音频设备索引（-1 表示未找到或不需要）
    
    // 设备能力（可选）
    std::vector<std::string> supported_resolutions;  // 支持的分辨率列表
    std::vector<int> supported_fps;                  // 支持的帧率列表
};

} // namespace gateway

#endif // GATEWAY_LOCAL_CAMERA_LOCAL_CAMERA_DEVICE_HPP

