#include "utils/ffmpeg_params.hpp"
#include <sstream>

namespace utils {

void FFmpegParams::AddVideoBitrateParams(std::ostringstream& oss, int bitrate_kbps) {
    if (bitrate_kbps > 0) {
        // 优化码率控制参数：
        // - bufsize: 设置为码率的 2 倍，提供足够的缓冲空间，提升编码稳定性
        //   对于实时流，2 倍码率是较好的平衡（延迟和稳定性）
        // - 使用 ABR（平均码率）模式，在保持平均码率的同时允许小幅波动，提升画质
        //   注意：rc-lookahead 已在 x264-params 中设置，这里不再重复
        //   - bufsize: 设置为码率的 0.25 倍 (0.25s)，极低延迟
        int bufsize = bitrate_kbps / 4;  
        oss << " -b:v " << bitrate_kbps << "k"
            << " -maxrate " << bitrate_kbps << "k"
            << " -bufsize " << bufsize << "k";
        // 注意：rc-lookahead 已在 x264-params 中设置，这里不再重复设置
        // 使用默认的 ABR（平均码率）模式，在保持平均码率的同时允许小幅波动
    }
}

void FFmpegParams::AddRateLimitParams(std::ostringstream& oss, int bitrate_kbps) {
    if (bitrate_kbps > 0) {
        oss << " -maxrate " << bitrate_kbps << "k"
            << " -bufsize " << (bitrate_kbps * 2) << "k";
    }
}

void FFmpegParams::AddWebRTCEncodingParams(std::ostringstream& oss, 
                                           int bitrate_kbps, 
                                           bool include_fpsmax,
                                           int source_width,
                                           int source_height) {
    (void)include_fpsmax;  // 参数已废弃，保留以保持 API 兼容性
    int final_bitrate = (bitrate_kbps > 0) ? bitrate_kbps : 2500;
    int gop_size = 15;  // WebRTC 推荐的小 GOP，降低延迟
    // 注意：对于avfoundation输入，-tune zerolatency可能导致绿幕问题
    // 尝试移除-tune zerolatency，只使用x264-params来控制延迟
    // 如果仍有问题，可以考虑使用-tune fastdecode或其他tune选项
    oss << " -c:v libx264 -preset veryfast"
        << " -profile:v baseline -level 3.0"  // 关键：WebRTC 必须使用 Baseline Profile 以保证最大兼容性
        << " -pix_fmt yuv420p"  // 关键：强制 YUV420P 格式，防止 nv12 等格式导致浏览器无法解码
        << " -g " << gop_size;
    
    // 优化线程数：低延迟模式下使用较少线程，减少帧缓冲
    // 0 = 自动 (多线程缓冲)，1 = 单线程 (最低延迟，但 CPU 单核负载高)
    // 对于 720p/1080p，现代 CPU 单核足够编码
    oss << " -threads 1";
    
    // 智能分辨率处理：保持源流分辨率，不强制缩放
    if (source_width > 0 && source_height > 0) {
        // 保持源流分辨率，不添加 -vf scale
        // 这样可以避免不必要的缩放，保持源流质量
    } else {
        // 无法检测分辨率，使用默认缩放（保持向后兼容）
        oss << " -vf scale=1920:1080";
    }
    oss << " -r 30";  // 提升到 30fps，与 HTTP-FLV/HLS 一致，充分利用分配的码率
    // 优化后的 x264 参数：尽可能降低延迟
    // 注意：已移除 -tune zerolatency，因为它在 macOS avfoundation 输入下会导致绿屏
    // 我们仅使用 x264-params 来手动控制延迟相关的参数
    oss << " -x264-params keyint=" << gop_size 
        << ":min-keyint=" << gop_size 
        << ":scenecut=0:bframes=0:ref=1:rc-lookahead=0:no-mbtree=1"; // bframes=0, lookahead=0, mbtree=0 都是为了低延迟
    AddVideoBitrateParams(oss, final_bitrate);
    // 注意：虽然 WebRTC 最终需要 Opus 音频，但推流到 ZLM 时使用 FLV 容器
    // FLV 容器不支持 Opus，只支持 AAC/MP3/PCM，所以这里使用 AAC
    // ZLM 会从 FLV 中提取 AAC 音频，并在 WebRTC 输出时自动转换为 Opus
    // 使用 AAC 参数：48kHz 采样率（与 WebRTC 标准一致），立体声，128kbps 码率
    oss << " -c:a aac -b:a 128k -ar 48000 -ac 2";
    // 注意：不能同时使用 -r 和 -fpsmax，FFmpeg 会报错 "Only one of -fpsmax and -r can be set for a stream"
    // 使用固定帧率 -r 30 而不是 -fpsmax，确保稳定的帧率输出
}

void FFmpegParams::AddStreamingEncodingParams(std::ostringstream& oss, 
                                               int bitrate_kbps,
                                               int source_width,
                                               int source_height,
                                               const std::string& output_protocol) {
    int final_bitrate = (bitrate_kbps > 0) ? bitrate_kbps : 3500;
    
    // 根据输出协议智能调整 GOP 大小，降低首屏延迟
    // HTTP-FLV: GOP=18（平衡延迟和质量，约 0.6 秒首屏延迟 @ 30fps）
    // HLS: GOP=12（降低首屏延迟，约 0.4 秒首屏延迟 @ 30fps）
    // 默认: GOP=18（如果未指定协议）
    int gop_size = 18;  // 默认值
    if (output_protocol == "hls") {
        gop_size = 12;  // HLS 使用更小的 GOP，降低首屏延迟
    } else if (output_protocol == "http-flv") {
        gop_size = 18;  // HTTP-FLV 平衡延迟和质量
    }
    
    // 为解决播放器缓冲问题，恢复 zerolatency 调优
    oss << " -c:v libx264 -preset veryfast -tune zerolatency"
        << " -g " << gop_size;
    
    // ... (threads part)
    oss << " -threads 0"; 

    // ... (scale part)
    if (source_width > 0 && source_height > 0) {
        // 保持源流分辨率
    } else {
        oss << " -vf scale=1920:1080";
    }
    
    // 简化 x264 参数，配合 zerolatency
    // bframes=0 (zerolatency 默认), rc-lookahead=0
    oss << " -x264-params keyint=" << gop_size 
        << ":min-keyint=" << gop_size 
        << ":scenecut=0";
    
    AddVideoBitrateParams(oss, final_bitrate);
    oss << " -c:a aac -b:a 128k -ar 44100 -ac 2";
}

} // namespace utils

