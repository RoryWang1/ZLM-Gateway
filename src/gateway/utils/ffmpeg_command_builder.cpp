#include "gateway/utils/ffmpeg_command_builder.hpp"
#include "utils/logger.hpp"
#include "utils/ffmpeg_params.hpp"
#include "utils/zlm_url_builder.hpp"
#include <sstream>
#include <algorithm>
#include <cmath>

namespace gateway {
namespace utils {

FFmpegCommandBuilder::FFmpegCommandBuilder(std::shared_ptr<config::Config> config,
                                           const std::string& ffmpeg_path,
                                           const std::string& zlm_rtmp_url)
    : config_(config), ffmpeg_path_(ffmpeg_path), zlm_rtmp_url_(zlm_rtmp_url) {
}

std::string FFmpegCommandBuilder::BuildCommand(const FFmpegCommandOptions& options) const {
    std::ostringstream oss;
    oss << ffmpeg_path_;

    // 全局参数（针对网络流）
    // 优化：将 analyzeduration 设为 3s (平衡启动速度和探测准确性)
    if (options.input_format.empty() || options.input_format == "rtsp" || options.input_format == "flv" || options.input_format == "hls") {
        // 降低为 1s 以加快 HLS -re 模式下的启动速度
        oss << " -analyzeduration 1000000"
            << " -probesize 1000000";
    }

    // 1. 构建输入
    BuildInput(oss, options);

    // 2. 构建编码参数
    BuildEncoding(oss, options);

    // 3. 构建输出
    BuildOutput(oss, options);
    
    // 结束引号 (如果有必要，取决于 BuildOutput 实现，这里假定不需要额外引号闭合整个命令，而是每个参数段自己处理)
    
    return oss.str();
}

void FFmpegCommandBuilder::BuildInput(std::ostringstream& oss, const FFmpegCommandOptions& options) const {
    if (options.input_format == "avfoundation" || options.input_format == "v4l2") {
        BuildDeviceInput(oss, options);
    } else {
        BuildNetworkInput(oss, options);
    }
}

void FFmpegCommandBuilder::BuildDeviceInput(std::ostringstream& oss, const FFmpegCommandOptions& options) const {
#ifdef __APPLE__
    // macOS: avfoundation
    int input_fps = options.input_fps > 0 ? options.input_fps : config_->local_camera.default_fps;
    
    oss << " -f avfoundation";
    if (options.input_width > 0 && options.input_height > 0) {
        oss << " -video_size " << options.input_width << "x" << options.input_height;
    }
    oss << " -framerate " << input_fps;
    
    // 音频输入处理
    std::string audio_input = ":default";
    if (!options.audio_input_format.empty()) {
        audio_input = options.audio_input_format;
    }
    
    oss << " -i \"" << options.input_url << audio_input << "\"";
#elif __linux__
    // Linux: v4l2
    int input_fps = options.input_fps > 0 ? options.input_fps : config_->local_camera.default_fps;
    oss << " -f v4l2 -framerate " << input_fps << " -i " << options.input_url;
#else
    LOG_ERROR("[FFmpegCommandBuilder] Unsupported OS for device input");
#endif
}

void FFmpegCommandBuilder::BuildNetworkInput(std::ostringstream& oss, const FFmpegCommandOptions& options) const {
    if (options.input_format == "rtsp") {
        oss << " -rtsp_transport tcp";
    }
    
    // HLS 检测：格式明确为 hls 或 URL 以 .m3u8 结尾且为 HTTP(S) This robustness fix handles cases where input_format is 'auto'
    bool is_hls = (options.input_format == "hls") || 
                 ((options.input_url.find(".m3u8") != std::string::npos || options.input_url.find(".M3U8") != std::string::npos) && 
                  (options.input_url.find("http://") == 0 || options.input_url.find("https://") == 0));

    if (is_hls) {
        // HLS 输入通常需要 -re (Read at native frame rate)
        // 1. 如果是 VOD HLS (如测试源)，不加 -re 会导致 ffmpeg 全速读取，结合 -use_wallclock 会导致时间戳压缩，播放极快或卡顿
        // 2. 如果是 Live HLS，加 -re 通常无害 (源本身就是实时的)
        oss << " -re";
    } else {
        // 使用挂钟时间作为时间戳，解决 RTSP/网络流时间戳抖动问题
        // 这对于 copy 模式下生成平滑的 FLV 流至关重要
        // 注意：对于 HLS VOD，开启此选项会破坏原始时间戳（尤其在 -re 失效导致读取过快时），导致播放加速
        // 因此只对非 HLS 输入启用
        oss << " -use_wallclock_as_timestamps 1";
    }
    
    // NOTE: stream_loop doesn't work with HTTP sources in FFmpeg
    // For finite test files, streams will end after source completes
    oss << " -i \"" << options.input_url << "\"";
}

void FFmpegCommandBuilder::BuildEncoding(std::ostringstream& oss, const FFmpegCommandOptions& options) const {
    if (options.output_protocol == "webrtc") {
        BuildWebRTCEncoding(oss, options);
    } else if (options.output_protocol == "http-flv" || options.output_protocol == "hls") {
        BuildStreamingEncoding(oss, options);
    } else {
        BuildDefaultEncoding(oss, options);
    }
}

void FFmpegCommandBuilder::BuildWebRTCEncoding(std::ostringstream& oss, const FFmpegCommandOptions& options) const {
    // WebRTC 模式：必须转码为 H.264 (High profile, zero latency) + AAC (最终转Opus)
    // 且必须确保分辨率和像素格式正确
    
    // 码率选择
    const auto& webrtc_cfg = config_->local_camera.webrtc;
    int bitrate = (options.target_bitrate_kbps > 0) ? options.target_bitrate_kbps : 
                 (webrtc_cfg.bitrate.empty() ? 2500 : std::stoi(webrtc_cfg.bitrate));

    // 分辨率选择
    int target_width, target_height;
    bool use_native_resolution = false;
    
    int src_width = options.stream_info.video_width > 0 ? options.stream_info.video_width : options.input_width;
    int src_height = options.stream_info.video_height > 0 ? options.stream_info.video_height : options.input_height;

    if (src_width > 0 && src_height > 0 && options.use_native_resolution) {
        target_width = src_width;
        target_height = src_height;
        use_native_resolution = true;
    } else {
        std::string config_res = webrtc_cfg.resolution.empty() ? "1920x1080" : webrtc_cfg.resolution;
        auto res = SelectResolution(config_res, src_width, src_height);
        target_width = res.first;
        target_height = res.second;
    }

    // 像素格式 (设备输入通常需要 nv12，网络流通常已经是 yuv420p 但统一 nv12 更安全)
    if (options.input_format == "avfoundation") {
        oss << " -pix_fmt nv12";
    }

    // 使用公共辅助类生成核心编码参数
    // 传递source的宽高，让AddWebRTCEncodingParams决定是否添加scale
    ::utils::FFmpegParams::AddWebRTCEncodingParams(oss, bitrate, true, src_width, src_height);

    // 显式指定输出分辨率（使用-s参数，不使用-vf避免冲突）
    oss << " -s " << target_width << "x" << target_height;

    // 音频处理：如果是 AAC 且为网络流，尝试 Copy？
    // WebRTC 比较特殊，建议统一转码以保证兼容性，除非非常确定源完全兼容
    // 目前策略：简单点，如果是网络流且是 AAC，尝试 Copy；否则转码
    if (options.input_format != "avfoundation" && options.input_format != "v4l2" && options.stream_info.audio_codec == "aac") {
         LOG_DEBUG("[FFmpegCommandBuilder] WebRTC: Source is AAC, logic suggests copy but params enforced transcoding. Keeping transcoding for stability.");
         // 注意：AddWebRTCEncodingParams 已经加了 -c:a aac。如果想 Copy 需要覆盖。
         // 鉴于 WebRTC 对音频参数敏感，这里维持转码
    }
}

void FFmpegCommandBuilder::BuildStreamingEncoding(std::ostringstream& oss, const FFmpegCommandOptions& options) const {
    // HTTP-FLV/HLS 模式：智能转码
    const auto& flv_hls_cfg = config_->local_camera.flv_hls;
    int bitrate = (options.target_bitrate_kbps > 0) ? options.target_bitrate_kbps : 
                 (flv_hls_cfg.bitrate.empty() ? 1800 : std::stoi(flv_hls_cfg.bitrate));

    // 分辨率选择
    int src_width = options.stream_info.video_width > 0 ? options.stream_info.video_width : options.input_width;
    int src_height = options.stream_info.video_height > 0 ? options.stream_info.video_height : options.input_height;
    
    std::string config_res = flv_hls_cfg.resolution.empty() ? "1920x1080" : flv_hls_cfg.resolution;
    auto [target_width, target_height] = SelectResolution(config_res, src_width, src_height);

    // 智能 Copy 判断 (仅针对网络流)
    bool is_device = (options.input_format == "avfoundation" || options.input_format == "v4l2");
    // 智能 Copy 判断 (仅针对网络流)
    // 恢复智能Copy，通过输入参数调优解决Jitter问题
    // 智能 Copy 判断 (仅针对网络流)
    // 恢复智能Copy，通过输入参数调优解决Jitter问题
    // HLS 检测 (需要在 BuildStreamingEncoding 中重新检测或传递，这里暂重复逻辑)
    bool is_hls = (options.input_format == "hls") || 
            ((options.input_url.find(".m3u8") != std::string::npos || options.input_url.find(".M3U8") != std::string::npos) && 
            (options.input_url.find("http://") == 0 || options.input_url.find("https://") == 0));

    if (!is_device && !is_hls) {
        // ... (existing copy logic) ...
        bool video_compatible = ::gateway::utils::StreamInfoDetector::IsVideoCodecCompatible(options.stream_info.video_codec);
        bool audio_compatible = ::gateway::utils::StreamInfoDetector::IsAudioCodecCompatible(options.stream_info.audio_codec);
        
        if (video_compatible && audio_compatible) {
             // 调试日志：检查为何滤镜未添加
             // std::cout << "[DEBUG] BuildStreamingEncoding: protocol=" << options.output_protocol 
             //           << ", codec=" << options.stream_info.audio_codec << std::endl;

             // 策略调整：对于网络流（RTSP/RTMP），为了解决 ADTS/ASC 封装兼容性问题（导致VLC无声/浏览器卡顿）
             // 我们强制进行音频转码，保留视频 Copy。音频转码开销极低，但能保证兼容性。
             // 视频兼容 -> Copy Video
             oss << " -c:v copy";
             if (bitrate > 0) ::utils::FFmpegParams::AddRateLimitParams(oss, bitrate);
             
             // 强制转码音频 (修复 ADTS -> FLV/RTMP 问题)
             oss << " -c:a aac -b:a 128k -ar 44100 -ac 2";
             return;
        } else if (video_compatible) {
            // 视频兼容 -> Copy Video
            oss << " -c:v copy";
            if (bitrate > 0) ::utils::FFmpegParams::AddRateLimitParams(oss, bitrate);
            
            // 转码音频
            oss << " -c:a aac -b:a 128k -ar 44100 -ac 2";
            return;
        }
    }

    // 需要转码
    // 传递source_width/height让AddStreamingEncodingParams知道源分辨率，避免添加-vf scale
    ::utils::FFmpegParams::AddStreamingEncodingParams(oss, bitrate, src_width, src_height, options.output_protocol);

    // 构建滤镜链
    std::string filters;
    
    // HLS VOD 需要 realtime 滤镜来控制播放速度 (解决 -re 读取过快问题)
    if (is_hls) {
        filters += "realtime,";
    }

    // 缩放处理
    if (src_width > 0 && src_height > 0 && (src_width != target_width || src_height != target_height)) {
        // 需要缩放到不同分辨率
        // 注意：AddStreamingEncodingParams在src>0时不会添加-vf，所以这里安全
        filters += "scale=" + std::to_string(target_width) + ":" + std::to_string(target_height) + 
                   ":force_original_aspect_ratio=decrease,pad=" + std::to_string(target_width) + 
                   ":" + std::to_string(target_height) + ":(ow-iw)/2:(oh-ih)/2,";
    }

    // 移除末尾逗号并添加到命令
    if (!filters.empty()) {
        if (filters.back() == ',') {
            filters.pop_back();
        }
        oss << " -vf \"" << filters << "\"";
    }
    // 否则保持原分辨率（不添加-vf scale），遵循"尽量不破坏原视频"原则
    
    // FPS
    if (flv_hls_cfg.fps > 0) {
        oss << " -r " << flv_hls_cfg.fps;
    }
}

void FFmpegCommandBuilder::BuildDefaultEncoding(std::ostringstream& oss, const FFmpegCommandOptions& options) const {
    // 默认模式：尽可能 Copy
    bool is_device = (options.input_format == "avfoundation" || options.input_format == "v4l2");
    
    if (is_device) {
        // 设备输入必须转码
        int bitrate = (options.target_bitrate_kbps > 0) ? options.target_bitrate_kbps : 
                     (config_->local_camera.default_bitrate.empty() ? 1800 : std::stoi(config_->local_camera.default_bitrate));
        
        ::utils::FFmpegParams::AddVideoBitrateParams(oss, bitrate);
        oss << " -c:v libx264 -preset " << config_->local_camera.encoding_preset 
            << " -tune " << config_->local_camera.encoding_tune
            << " -g 25 -r 30"
            << " -c:a aac -b:a 128k -ar 44100 -ac 2";
        
        // 分辨率处理... 略简略，默认不缩放如果未指定
    } else {
        // 网络流
        bool video_compatible = ::gateway::utils::StreamInfoDetector::IsVideoCodecCompatible(options.stream_info.video_codec);
        
        if (video_compatible) {
            oss << " -c:v copy";
            // 强制转码音频 (修复 ADTS -> FLV/RTMP 问题)
            oss << " -c:a aac -b:a 128k -ar 44100 -ac 2";
        } else {
             oss << " -c:v libx264 -preset veryfast -g 25";
             ::utils::FFmpegParams::AddVideoBitrateParams(oss, options.target_bitrate_kbps > 0 ? options.target_bitrate_kbps : 3500);
             oss << " -c:a aac -b:a 128k -ar 44100 -ac 2";
        }
        
        if (options.target_bitrate_kbps > 0) {
             ::utils::FFmpegParams::AddRateLimitParams(oss, options.target_bitrate_kbps);
        }

        /* 
        // 旧逻辑：尝试 Copy 音频 (已废弃)
        if (options.stream_info.audio_codec == "aac") {
            oss << " -c:a copy";
        } else {
            oss << " -c:a aac -b:a 128k -ar 44100 -ac 2";
        }
        */
    }
}

void FFmpegCommandBuilder::BuildOutput(std::ostringstream& oss, const FFmpegCommandOptions& options) const {
    // 统一输出为 FLV 推流
    oss << " -f flv \"" 
        << ::utils::zlm_url_builder::BuildRTMPUrlWithSecret(
               config_, options.target_app, options.target_stream, options.gateway_name) 
        << "\"";
}

std::pair<int, int> FFmpegCommandBuilder::SelectResolution(const std::string& config_resolution, 
                                                           int native_width, int native_height) const {
    int config_width = 1920, config_height = 1080;
    size_t x_pos = config_resolution.find('x');
    if (x_pos != std::string::npos) {
        try {
            config_width = std::stoi(config_resolution.substr(0, x_pos));
            config_height = std::stoi(config_resolution.substr(x_pos + 1));
        } catch (...) {}
    }

    if (native_width > 0 && native_height > 0) {
        int width_diff = std::abs(native_width - config_width);
        int height_diff = std::abs(native_height - config_height);
        // 误差 10% 以内则认为匹配
        if (width_diff * 10 <= config_width && height_diff * 10 <= config_height) {
            return {native_width, native_height};
        }
    }
    return {config_width, config_height};
}

} // namespace utils
} // namespace gateway
