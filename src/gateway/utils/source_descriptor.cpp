#include "gateway/utils/source_descriptor.hpp"
#include "utils/logger.hpp"
#include <algorithm>
#include <sstream>

namespace gateway {
namespace utils {

SourceDescriptor SourceDescriptor::FromURL(const std::string& url) {
    SourceDescriptor desc;
    desc.source_url = url;
    
    // 根据 URL scheme 判断类型
    if (url.find("rtsp://") == 0) {
        desc.type = SourceType::RTSP_URL;
    } else if (url.find("rtmp://") == 0) {
        desc.type = SourceType::RTMP_URL;
    } else if (url.find("http://") == 0 || url.find("https://") == 0) {
        // 根据文件扩展名进一步判断
        if (url.find(".m3u8") != std::string::npos) {
            desc.type = SourceType::HLS_URL;
        } else if (url.find(".mpd") != std::string::npos) {
            desc.type = SourceType::DASH_URL;
        } else if (url.find(".flv") != std::string::npos) {
            desc.type = SourceType::HTTP_FLV_URL;
        } else {
            // 默认当作 HTTP-FLV
            desc.type = SourceType::HTTP_FLV_URL;
        }
    } else if (url.find("quic://") == 0) {
        desc.type = SourceType::QUIC_URL;
    } else {
        // 默认当作 RTSP
        desc.type = SourceType::RTSP_URL;
    }
    
    return desc;
}

SourceDescriptor SourceDescriptor::FromLocalCamera(const std::string& device_id,
                                                   int camera_index,
                                                   const std::string& camera_name,
                                                   int audio_device_index) {
    SourceDescriptor desc;
    desc.type = SourceType::LOCAL_CAMERA;
    desc.source_url = "local-camera://device_id:" + device_id;
    desc.local_camera.device_id = device_id;
    desc.local_camera.camera_index = camera_index;
    desc.local_camera.camera_name = camera_name;
    desc.local_camera.audio_device_index = audio_device_index;
    return desc;
}

SourceDescriptor SourceDescriptor::FromDevice(SourceType device_type,
                                              const std::string& device_id,
                                              const std::string& channel_id,
                                              const std::string& profile_token) {
    SourceDescriptor desc;
    desc.type = device_type;
    
    // 构建 source_url
    std::ostringstream oss;
    switch (device_type) {
        case SourceType::DEVICE_ONVIF:
            oss << "onvif://" << device_id;
            if (!profile_token.empty()) {
                oss << "/" << profile_token;
            }
            break;
        case SourceType::DEVICE_ISAPI:
            oss << "isapi://" << device_id;
            if (!channel_id.empty()) {
                oss << "/" << channel_id;
            }
            break;
        case SourceType::DEVICE_DAHUA:
            oss << "dahua://" << device_id;
            if (!channel_id.empty()) {
                oss << "/" << channel_id;
            }
            break;
        case SourceType::DEVICE_PSIA:
            oss << "psia://" << device_id;
            if (!channel_id.empty()) {
                oss << "/" << channel_id;
            }
            break;
        default:
            oss << "device://" << device_id;
            break;
    }
    desc.source_url = oss.str();
    
    desc.device.device_id = device_id;
    desc.device.channel_id = channel_id;
    desc.device.profile_token = profile_token;
    
    return desc;
}

std::string SourceDescriptor::SourceTypeToString(SourceType type) {
    switch (type) {
        case SourceType::RTSP_URL: return "rtsp_url";
        case SourceType::RTMP_URL: return "rtmp_url";
        case SourceType::HTTP_FLV_URL: return "http_flv_url";
        case SourceType::HLS_URL: return "hls_url";
        case SourceType::DASH_URL: return "dash_url";
        case SourceType::QUIC_URL: return "quic_url";
        case SourceType::LOCAL_CAMERA: return "local_camera";
        case SourceType::LOCAL_FILE: return "local_file";
        case SourceType::SCREEN_CAPTURE: return "screen_capture";
        case SourceType::DEVICE_ONVIF: return "device_onvif";
        case SourceType::DEVICE_ISAPI: return "device_isapi";
        case SourceType::DEVICE_DAHUA: return "device_dahua";
        case SourceType::DEVICE_PSIA: return "device_psia";
        default: return "unknown";
    }
}

SourceType SourceDescriptor::SourceTypeFromString(const std::string& type_str) {
    if (type_str == "rtsp_url" || type_str == "rtsp") {
        return SourceType::RTSP_URL;
    } else if (type_str == "rtmp_url" || type_str == "rtmp") {
        return SourceType::RTMP_URL;
    } else if (type_str == "http_flv_url" || type_str == "http-flv") {
        return SourceType::HTTP_FLV_URL;
    } else if (type_str == "hls_url" || type_str == "hls") {
        return SourceType::HLS_URL;
    } else if (type_str == "dash_url" || type_str == "dash") {
        return SourceType::DASH_URL;
    } else if (type_str == "quic_url" || type_str == "quic") {
        return SourceType::QUIC_URL;
    } else if (type_str == "local_camera" || type_str == "local-camera") {
        return SourceType::LOCAL_CAMERA;
    } else if (type_str == "local_file" || type_str == "file") {
        return SourceType::LOCAL_FILE;
    } else if (type_str == "screen_capture" || type_str == "screen") {
        return SourceType::SCREEN_CAPTURE;
    } else if (type_str == "device_onvif" || type_str == "onvif") {
        return SourceType::DEVICE_ONVIF;
    } else if (type_str == "device_isapi" || type_str == "isapi") {
        return SourceType::DEVICE_ISAPI;
    } else if (type_str == "device_dahua" || type_str == "dahua") {
        return SourceType::DEVICE_DAHUA;
    } else if (type_str == "device_psia" || type_str == "psia") {
        return SourceType::DEVICE_PSIA;
    } else {
        return SourceType::RTSP_URL;  // 默认
    }
}

} // namespace utils
} // namespace gateway

