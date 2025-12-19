#ifndef GATEWAY_UTILS_STREAM_INFO_DETECTOR_HPP
#define GATEWAY_UTILS_STREAM_INFO_DETECTOR_HPP

#include "process/ffprobe_detector.hpp"
#include <algorithm>
#include <memory>
#include <string>
#include <vector>
// Forward declaration to avoid full include if possible, but struct is nested.
// Use full include for simplicity as it's a utility class.
#include "config/config_loader.hpp"

namespace gateway {
namespace utils {

/**
 * @brief 流信息检测结果（便捷访问接口）
 *
 * 封装 FFprobeDetector 的 StreamInfo，提供便捷的访问方法
 */
struct StreamInfoResult {
  bool valid = false; // 检测是否成功

  // 音频信息
  std::string audio_codec;   // 音频编码（如 "aac", "pcm_alaw"）
  int audio_sample_rate = 0; // 音频采样率
  int audio_channels = 0;    // 音频声道数
  int audio_bitrate = 0;     // 音频码率（bps）

  // 视频信息
  std::string video_codec; // 视频编码（如 "h264", "h265"）
  int video_width = 0;     // 视频宽度
  int video_height = 0;    // 视频高度
  double video_fps = 0.0;  // 视频帧率

  int video_bitrate = 0;     // 视频码率（bps）
  std::string video_profile; // 视频 Profile (Baseline, Main, High)
  std::string video_level;   // 视频 Level (3.1, 4.0, etc.)
  std::string pixel_format;  // 像素格式 (yuv420p, etc.)

  // 流信息
  std::string format_name; // 容器格式（如 "flv", "rtsp"）
  int total_bitrate = 0;   // 总码率（bps）

  /**
   * @brief 从 FFprobeDetector::StreamInfo 构造
   */
  static StreamInfoResult
  FromStreamInfo(const process::StreamInfo &stream_info);
};

/**
 * @brief 统一的流信息检测工具
 *
 * 封装 FFprobeDetector，提供便捷的流信息检测接口
 * 所有 Gateway 都应该使用这个工具来检测流信息
 */
class StreamInfoDetector {
public:
  /**
   * @brief 构造函数
   * @param ffprobe_path FFprobe 路径（空则使用系统 PATH 中的 ffprobe）
   */
  explicit StreamInfoDetector(const std::string &ffprobe_path = "");

  /**
   * @brief 检测流信息
   * @param source_url 源流 URL
   * @param timeout_seconds 超时时间（秒，默认 10 秒）
   * @return 流信息检测结果
   */
  StreamInfoResult Detect(const std::string &source_url,
                          int timeout_seconds = 10);

  /**
   * @brief 检测流信息（带认证）
   * @param source_url 源流 URL
   * @param username 用户名
   * @param password 密码
   * @param timeout_seconds 超时时间（秒）
   * @return 流信息检测结果
   */
  StreamInfoResult Detect(const std::string &source_url,
                          const std::string &username,
                          const std::string &password,
                          int timeout_seconds = 10);

  /**
   * @brief 检查视频编码是否兼容（H.264/H.265）
   * @param video_codec 视频编码名称
   * @return 是否兼容
   */
  static bool IsVideoCodecCompatible(const std::string &video_codec);

  /**
   * @brief 检查音频编码是否兼容（AAC）
   * @param audio_codec 音频编码名称
   * @return 是否兼容
   */
  static bool IsAudioCodecCompatible(const std::string &audio_codec);

  /**
   * @brief 检查是否可以完全使用 copy（视频和音频都兼容）
   * @param result 流信息检测结果
   * @return 是否可以使用 -c copy
   */
  static bool CanUseFullCopy(const StreamInfoResult &result);

  /**
   * @brief 检查是否可以只转码音频（视频兼容，音频不兼容）
   * @param result 流信息检测结果
   * @return 是否可以使用 -c:v copy -c:a aac
   */
  static bool CanUseVideoCopy(const StreamInfoResult &result);

  /**
   * @brief [核心] 检查流是否完全兼容 WebRTC（基于配置策略）
   *
   * 这是一个"一锤定音"的函数，结合了 Profile、PixelFormat、AudioCodec 的检查。
   *
   * @param info 流信息
   * @param config WebRTC 兼容性配置
   * @param reason [输出]如果不兼容，写入具体原因（用于前端展示和日志）
   * @return true=兼容(Direct Proxy), false=不兼容(Transcode)
   */
  static bool IsWebRTCCompatible(
      const StreamInfoResult &info,
      const config::Config::GatewayConfig::WebRTCCompatibilityConfig &config,
      std::string &reason);

private:
  std::unique_ptr<process::FFprobeDetector> detector_;
};

} // namespace utils
} // namespace gateway

#endif // GATEWAY_UTILS_STREAM_INFO_DETECTOR_HPP
