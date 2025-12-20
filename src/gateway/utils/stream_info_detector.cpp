#include "gateway/utils/stream_info_detector.hpp"
#include "utils/logger.hpp"
#include <algorithm>
#include <cctype>

namespace gateway {
namespace utils {

StreamInfoResult StreamInfoResult::FromStreamInfo(const process::StreamInfo& stream_info) {
    StreamInfoResult result;
    
    if (stream_info.format_name.empty()) {
        result.valid = false;
        return result;
    }
    
    result.valid = true;
    result.format_name = stream_info.format_name;
    
    // 遍历所有编码器，提取音频和视频信息
    int total_bitrate = 0;
    for (const auto& codec : stream_info.codecs) {
        if (codec.codec_type == "audio") {
            result.audio_codec = codec.codec_name;
            result.audio_sample_rate = codec.sample_rate;
            result.audio_channels = codec.channels;
            result.audio_bitrate = codec.bitrate;
            total_bitrate += codec.bitrate;
        } else if (codec.codec_type == "video") {
            result.video_codec = codec.codec_name;
            result.video_width = codec.width;
            result.video_height = codec.height;
            result.video_fps = codec.fps;
            result.video_bitrate = codec.bitrate;
            result.video_profile = codec.profile;       // 【新增】
            result.video_level = codec.level;           // 【新增】
            result.pixel_format = codec.pixel_format;   // 【新增】
            total_bitrate += codec.bitrate;
        }
    }
    
    // 如果各个 codec 的 bitrate 为 0，但 format 的 bitrate 不为 0，使用 format 的 bitrate
    if (total_bitrate == 0 && stream_info.total_bitrate > 0) {
        result.total_bitrate = stream_info.total_bitrate;
    } else {
        result.total_bitrate = total_bitrate;
    }
    
    // 如果仍然为0，尝试基于分辨率、帧率估算码率（用于码率分配）
    // 这样可以确保即使检测失败，也能使用合理的码率
    if (result.total_bitrate == 0 && result.video_width > 0 && result.video_height > 0 && result.video_fps > 0) {
        // 基于分辨率和帧率估算码率（粗略估算）
        // 公式: bitrate ≈ width * height * fps * 0.1 (粗略估算，单位: bps)
        // 这个估算值用于码率分配，确保不会因为检测失败而使用过低的码率
        int estimated_bitrate = static_cast<int>(result.video_width * result.video_height * result.video_fps * 0.1);
        if (estimated_bitrate > 0) {
            result.total_bitrate = estimated_bitrate;
            LOG_INFO("[StreamInfoDetector] 码率检测为0，基于分辨率 {}x{}@{}fps 估算码率: {} bps ({} kbps)", 
                     result.video_width, result.video_height, result.video_fps, estimated_bitrate, estimated_bitrate / 1000);
        }
    }
    
    return result;
}

StreamInfoDetector::StreamInfoDetector(const std::string& ffprobe_path) {
    detector_ = std::make_unique<process::FFprobeDetector>(ffprobe_path);
}

StreamInfoResult StreamInfoDetector::Detect(const std::string& source_url, int timeout_seconds) {
    if (!detector_) {
        LOG_WARN("[StreamInfoDetector] FFprobeDetector not initialized");
        return StreamInfoResult();
    }
    
    process::StreamInfo stream_info = detector_->DetectStreamInfo(source_url, timeout_seconds);
    return StreamInfoResult::FromStreamInfo(stream_info);
}

StreamInfoResult StreamInfoDetector::Detect(const std::string& source_url,
                                           const std::string& username,
                                           const std::string& password,
                                           int timeout_seconds) {
    if (!detector_) {
        LOG_WARN("[StreamInfoDetector] FFprobeDetector not initialized");
        return StreamInfoResult();
    }
    
    process::StreamInfo stream_info = detector_->DetectStreamInfo(source_url, username, password, timeout_seconds);
    return StreamInfoResult::FromStreamInfo(stream_info);
}

bool StreamInfoDetector::IsVideoCodecCompatible(const std::string& video_codec) {
    if (video_codec.empty()) {
        return false;
    }
    
    std::string lower_codec = video_codec;
    std::transform(lower_codec.begin(), lower_codec.end(), lower_codec.begin(), ::tolower);
    
    // H.264 和 H.265 都是兼容的
    return lower_codec == "h264" || lower_codec == "libx264" ||
           lower_codec == "h265" || lower_codec == "hevc" || lower_codec == "libx265";
}

bool StreamInfoDetector::IsAudioCodecCompatible(const std::string& audio_codec) {
    if (audio_codec.empty()) {
        return false;
    }
    
    std::string lower_codec = audio_codec;
    std::transform(lower_codec.begin(), lower_codec.end(), lower_codec.begin(), ::tolower);
    
    // AAC 是兼容的
    return lower_codec == "aac" || lower_codec == "libfdk_aac";
}

bool StreamInfoDetector::CanUseFullCopy(const StreamInfoResult& result) {
    if (!result.valid) {
        return false;
    }
    
    // 视频和音频都兼容，可以使用 -c copy
    return IsVideoCodecCompatible(result.video_codec) && 
           IsAudioCodecCompatible(result.audio_codec);
}

bool StreamInfoDetector::CanUseVideoCopy(const StreamInfoResult& result) {
    if (!result.valid) {
        return false;
    }
    
    // 视频兼容，音频不兼容，可以使用 -c:v copy -c:a aac
    return IsVideoCodecCompatible(result.video_codec) && 
           !IsAudioCodecCompatible(result.audio_codec);
}

// 辅助函数：不区分大小写的列表查找
static bool IsInList(const std::vector<std::string>& list, const std::string& value) {
    std::string lower_value = value;
    std::transform(lower_value.begin(), lower_value.end(), lower_value.begin(), ::tolower);
    
    for (const auto& item : list) {
        std::string lower_item = item;
        std::transform(lower_item.begin(), lower_item.end(), lower_item.begin(), ::tolower);
        
        if (lower_item == lower_value) {
            return true;
        }
        // 特殊处理 profile：如果列表中是 "Baseline"，那么 "Constrained Baseline" 也应该算作匹配 (包含了 Baseline 关键词)
        // 或者反过来，严格匹配。为了安全起见，这里采用严格匹配 + 宽容模式（用户配置决定）
        // 实际上 FFprobe 返回的 Profile 字符串需要精确匹配
    }
    return false;
}

bool StreamInfoDetector::IsVideoWebSafe(const StreamInfoResult& info, std::string& reason) {
    // 1. Check Codec (Must be H.264)
    if (info.video_codec != "h264") {
        reason = "Video codec is not H.264 (Current: " + info.video_codec + ")";
        return false;
    }

    // 2. Check Pixel Format (Critical for Web: must be yuv420p)
    // flv.js and most browsers do not support yuv422p or yuvj420p well in FLV container
    if (info.pixel_format != "yuv420p" && info.pixel_format != "yuvj420p") {
         // Allow yuvj420p as it is mostly compatible, but yuv422p/444p is definitely not
         if (info.pixel_format == "yuv422p" || info.pixel_format == "yuv444p" || info.pixel_format == "uyvy422") {
             reason = "Incompatible Pixel Format for Web: " + info.pixel_format + " (Required: yuv420p)";
             return false;
         }
         // For other formats, we might warn but proceed, or be strict. Let's be semi-strict.
    }

    reason = "Compatible (H.264 + Safe Pixel Format)";
    return true;
}

bool StreamInfoDetector::IsVideoWebRTCCompatible(const StreamInfoResult& info, 
                                               const config::Config::GatewayConfig::WebRTCCompatibilityConfig& config,
                                               std::string& reason) {
    if (!info.valid) {
        reason = "Stream Info Invalid";
        return false;
    }

    // 1. 检查像素格式
    if (!info.pixel_format.empty()) {
        if (!IsInList(config.allowed_pixel_formats, info.pixel_format)) {
            reason = "Pixel Format Incompatible: " + info.pixel_format;
            return false;
        }
    } else {
        // 如果无法检测像素格式，暂且放行？还是从严？
        // 通常 H.264 都是 yuv420p，如果不确定，视为风险
        // 这里选择记录警告但暂不强制，或者设为默认兼容
        // 实际上 FFprobe 一般都能拿到。如果拿不到，可能是 mjepg 或奇怪的东西
        // 策略：如果拿不到，视为不兼容
        reason = "Pixel Format Missing";
        return false;
    }

    // 2. 检查视频编码
    if (!IsVideoCodecCompatible(info.video_codec)) {
        reason = "Video Codec Incompatible: " + info.video_codec;
        return false;
    }
    
    // 3. 检查 Profile (仅针对 H.264)
    std::string lower_codec = info.video_codec;
    std::transform(lower_codec.begin(), lower_codec.end(), lower_codec.begin(), ::tolower);
    if (lower_codec.find("h264") != std::string::npos || lower_codec.find("264") != std::string::npos) {
        if (!info.video_profile.empty()) {
             if (!IsInList(config.allowed_profiles, info.video_profile)) {
                 reason = "H.264 Profile Incompatible: " + info.video_profile;
                 return false;
             }
        } else {
             // 如果拿不到 Profile，可能是不标准的流
             reason = "H.264 Profile Missing";
             return false;
        }
    }

    reason = "Video Compatible";
    return true;
}

bool StreamInfoDetector::IsWebRTCCompatible(const StreamInfoResult& info, 
                                            const config::Config::GatewayConfig::WebRTCCompatibilityConfig& config,
                                            std::string& reason) {
    // 1. 检查视频兼容性
    if (!IsVideoWebRTCCompatible(info, config, reason)) {
        return false;
    }

    // 2. 检查音频编码
    // 注意：WebRTC 实际上非常挑剔。如果不是 Opus/PCMA/PCMU，通常都需要转码。
    // AAC 在某些浏览器支持，但在 WebRTC 容器中并不总是有效。
    // 如果没有音频流，则不用转码音频
    if (!info.audio_codec.empty()) {
        if (!IsInList(config.allowed_audio_codecs, info.audio_codec)) {
            reason = "Audio Codec Incompatible: " + info.audio_codec;
            return false;
        }
    }

    reason = "Compatible";
    return true;
}

} // namespace utils
} // namespace gateway

