#include "utils/ffmpeg_params.hpp"
#include <sstream>

namespace utils {

void FFmpegParams::AddVideoBitrateParams(std::ostringstream &oss,
                                         int bitrate_kbps) {
  if (bitrate_kbps > 0) {
    // 优化码率控制参数：
    // - bufsize: 设置为码率的 2 倍，提供足够的缓冲空间，提升编码稳定性
    //   对于实时流，2 倍码率是较好的平衡（延迟和稳定性）
    // - 使用 ABR（平均码率）模式，在保持平均码率的同时允许小幅波动，提升画质
    //   注意：rc-lookahead 已在 x264-params 中设置，这里不再重复
    int bufsize = bitrate_kbps * 2; // 2 倍码率作为缓冲区大小
    oss << " -b:v " << bitrate_kbps << "k" << " -maxrate " << bitrate_kbps
        << "k" << " -bufsize " << bufsize << "k";
    // 注意：rc-lookahead 已在 x264-params 中设置，这里不再重复设置
    // 使用默认的 ABR（平均码率）模式，在保持平均码率的同时允许小幅波动
  }
}

void FFmpegParams::AddRateLimitParams(std::ostringstream &oss,
                                      int bitrate_kbps) {
  if (bitrate_kbps > 0) {
    oss << " -maxrate " << bitrate_kbps << "k" << " -bufsize "
        << (bitrate_kbps * 2) << "k";
  }
}

void FFmpegParams::AddWebRTCEncodingParams(std::ostringstream &oss,
                                           int bitrate_kbps,
                                           bool include_fpsmax,
                                           int source_width,
                                           int source_height) {
  (void)include_fpsmax; // 参数已废弃，保留以保持 API 兼容性
  int final_bitrate = (bitrate_kbps > 0) ? bitrate_kbps : 2500;
  int gop_size = 15; // WebRTC 推荐的小 GOP，降低延迟
  // Optimize for Zero Latency
  // Re-enable tune zerolatency for Linux/V4L2 where it is standard.
  // We manually set x264-params to ensure low latency even if tune is skipped
  // or overridden.
  // Optimize for Zero Latency
  // Use 'veryfast' to retain deblocking and quality, preventing artifacts.
  // We REMOVE 'tune zerolatency' as it may be converting pixel formats or
  // causing stride issues. We rely on manual x264-params for latency.
  oss << " -c:v libx264 -preset veryfast" << " -g " << gop_size;

  // 优化线程数：自动检测 CPU 核心数
  oss << " -threads 0";

  // Force yuv420p for WebRTC compatibility
  oss << " -pix_fmt yuv420p";

  // 智能分辨率处理：保持源流分辨率，不强制缩放
  if (source_width > 0 && source_height > 0) {
    // 保持源流分辨率
  } else {
    oss << " -vf scale=1920:1080";
  }
  oss << " -r 30"
      // x264 parameters:
      // - bframes=0 / rc-lookahead=0: Manual zero latency
      // - scenecut=40: Default safe value
      // - ref=3: Increased from 1 to improve quality/repair
      << " -x264-params keyint=" << gop_size << ":min-keyint=" << gop_size
      << ":scenecut=40:bframes=0:rc-lookahead=0:ref=3";
  AddVideoBitrateParams(oss, final_bitrate);
  // 注意：虽然 WebRTC 最终需要 Opus 音频，但推流到 ZLM 时使用 FLV 容器
  // FLV 容器不支持 Opus，只支持 AAC/MP3/PCM，所以这里使用 AAC
  // ZLM 会从 FLV 中提取 AAC 音频，并在 WebRTC 输出时自动转换为 Opus
  // 使用 AAC 参数：48kHz 采样率（与 WebRTC 标准一致），立体声，128kbps 码率
  oss << " -c:a aac -b:a 128k -ar 48000 -ac 2";
}

void FFmpegParams::AddStreamingEncodingParams(
    std::ostringstream &oss, int bitrate_kbps, int source_width,
    int source_height, const std::string &output_protocol) {
  int final_bitrate = (bitrate_kbps > 0) ? bitrate_kbps : 3500;

  // 根据输出协议智能调整 GOP 大小，降低首屏延迟
  // HTTP-FLV: GOP=18（平衡延迟和质量，约 0.6 秒首屏延迟 @ 30fps）
  // HLS: GOP=12（降低首屏延迟，约 0.4 秒首屏延迟 @ 30fps）
  // 默认: GOP=18（如果未指定协议）
  int gop_size = 18; // 默认值
  if (output_protocol == "hls") {
    gop_size = 12; // HLS 使用更小的 GOP，降低首屏延迟
  } else if (output_protocol == "http-flv") {
    gop_size = 18; // HTTP-FLV 平衡延迟和质量
  }

  // 为解决播放器缓冲问题，恢复 zerolatency 调优
  oss << " -c:v libx264 -preset veryfast -tune zerolatency" << " -g "
      << gop_size;

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
  oss << " -x264-params keyint=" << gop_size << ":min-keyint=" << gop_size
      << ":scenecut=0";

  AddVideoBitrateParams(oss, final_bitrate);
  // 音频参数移至 FFmpegCommandBuilder 处理以支持按需启用
}

} // namespace utils
