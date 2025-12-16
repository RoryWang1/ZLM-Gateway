#ifndef GATEWAY_UTILS_SOURCE_DESCRIPTOR_HPP
#define GATEWAY_UTILS_SOURCE_DESCRIPTOR_HPP

#include <string>
#include <map>
#include <variant>

namespace gateway {
namespace utils {

/**
 * @brief 源类型枚举
 */
enum class SourceType {
    RTSP_URL,        // RTSP URL（如 rtsp://192.168.1.100:554/stream）
    RTMP_URL,        // RTMP URL（如 rtmp://192.168.1.100:1935/live/stream）
    HTTP_FLV_URL,    // HTTP-FLV URL（如 http://192.168.1.100:8080/stream.flv）
    HLS_URL,         // HLS URL（如 http://192.168.1.100:8080/stream.m3u8）
    DASH_URL,        // DASH URL（如 http://192.168.1.100:8080/stream.mpd）
    QUIC_URL,        // QUIC URL
    LOCAL_CAMERA,    // 本地摄像头（通过设备ID和索引）
    LOCAL_FILE,      // 本地文件（如 /path/to/video.mp4）
    SCREEN_CAPTURE,  // 屏幕捕获（未来扩展）
    DEVICE_ONVIF,    // ONVIF 设备（通过设备ID）
    DEVICE_ISAPI,    // ISAPI 设备（通过设备ID）
    DEVICE_DAHUA,    // 大华设备（通过设备ID）
    DEVICE_PSIA      // PSIA 设备（通过设备ID）
};

/**
 * @brief 统一的源描述结构
 * 
 * 用于抽象不同类型的流源，供通用推流模板使用
 */
struct SourceDescriptor {
    SourceType type;  // 源类型
    
    // 通用参数（所有类型都可能用到）
    std::string source_url;  // 源URL（对于URL类型）或标识符（对于设备类型）
    
    // 本地摄像头特定参数
    struct LocalCameraParams {
        std::string device_id;      // 设备ID
        int camera_index = -1;      // 摄像头索引
        std::string camera_name;    // 摄像头名称
        int audio_device_index = -1; // 音频设备索引
    } local_camera;
    
    // 设备发现协议特定参数
    struct DeviceParams {
        std::string device_id;      // 设备ID
        std::string channel_id;     // 通道ID（可选）
        std::string profile_token;  // Profile Token（ONVIF，可选）
    } device;
    
    // 编码参数（可选，用于需要转码的场景）
    struct EncodingParams {
        std::string resolution;     // 分辨率（如 "1280x720"）
        int fps = 30;               // 帧率
        std::string bitrate;        // 码率（如 "2000k"）
        std::string video_codec;     // 视频编码器（如 "libx264"）
        std::string audio_codec;    // 音频编码器（如 "aac"）
        std::string preset;         // 编码预设（如 "veryfast"）
        std::string tune;           // 编码调优（如 "zerolatency"）
    } encoding;
    
    // 扩展参数（用于存储类型特定的额外信息）
    std::map<std::string, std::string> extra_params;
    
    /**
     * @brief 从 source_url 创建 SourceDescriptor（用于 URL 类型）
     */
    static SourceDescriptor FromURL(const std::string& url);
    
    /**
     * @brief 从本地摄像头参数创建 SourceDescriptor
     */
    static SourceDescriptor FromLocalCamera(const std::string& device_id,
                                           int camera_index,
                                           const std::string& camera_name = "",
                                           int audio_device_index = -1);
    
    /**
     * @brief 从设备参数创建 SourceDescriptor（用于设备发现协议）
     */
    static SourceDescriptor FromDevice(SourceType device_type,
                                      const std::string& device_id,
                                      const std::string& channel_id = "",
                                      const std::string& profile_token = "");
    
    /**
     * @brief 获取源类型的字符串表示
     */
    static std::string SourceTypeToString(SourceType type);
    
    /**
     * @brief 从字符串解析源类型
     */
    static SourceType SourceTypeFromString(const std::string& type_str);
};

} // namespace utils
} // namespace gateway

#endif // GATEWAY_UTILS_SOURCE_DESCRIPTOR_HPP

