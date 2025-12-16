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

} // namespace utils
} // namespace gateway

