#ifndef GATEWAY_UTILS_SMART_STREAM_PROCESSOR_HPP
#define GATEWAY_UTILS_SMART_STREAM_PROCESSOR_HPP

#include "gateway/base/gateway_base.hpp"
#include "gateway/utils/stream_info_detector.hpp"
#include "gateway/utils/stream_start_validator.hpp"
#include "process/process_manager.hpp"
#include "streaming/stream_manager.hpp"
#include "streaming/zlmediakit/zlm_client.hpp"
#include <functional>
#include <memory>
#include <string>

namespace gateway {
namespace utils {

/**
 * @brief 流处理结果
 */
struct StreamProcessResult {
  bool success = false;
  bool use_ffmpeg = false;           // 是否使用了 FFmpeg 转码
  bool video_only_transcode = false; // 是否只转码视频（音频使用 copy）
  int pid = 0;                       // 进程ID（如果使用 FFmpeg）
  GatewayStatus status = GatewayStatus::Stopped;
  std::string error_code;    // 错误码
  std::string error_message; // 错误消息

  // 检测到的流信息
  StreamInfoResult stream_info;

  // 源流信息（用于构建 FFmpeg 命令）
  std::string source_audio_codec;
  std::string source_video_codec;
  int source_width = 0;
  int source_height = 0;
};

/**
 * @brief 直接代理回调函数类型
 *
 * @param target_app 目标应用名
 * @param target_stream 目标流名
 * @param source_url 源流 URL
 * @return 是否成功调用 ZLM API（不保证流已活跃）
 */
using DirectProxyCallback = std::function<bool(const std::string &target_app,
                                               const std::string &target_stream,
                                               const std::string &source_url)>;

/**
 * @brief 获取流信息的回调函数类型
 *
 * @param target_app 目标应用名
 * @param target_stream 目标流名
 * @param schema 流协议 schema（如 "rtsp", "http-flv", "hls"）
 * @return 流信息
 */
using GetStreamInfoCallback = std::function<streaming::StreamInfo(
    const std::string &target_app, const std::string &target_stream,
    const std::string &schema)>;

/**
 * @brief 启动 FFmpeg 进程的回调函数类型
 *
 * @param stream_info_result 流信息检测结果
 * @return 进程ID，失败返回0
 */
using StartFFmpegCallback =
    std::function<int(const StreamInfoResult &stream_info_result)>;

/**
 * @brief 读取 FFmpeg 错误日志的回调函数类型
 *
 * @return 错误日志内容
 */
using ReadFFmpegErrorCallback = std::function<std::string()>;

/**
 * @brief 智能流处理器
 *
 * 封装所有 Gateway 的通用逻辑：
 * 1. 流信息检测（使用 StreamInfoDetector）
 * 2. 智能转码决策（基于检测结果）
 * 3. 直接代理验证（重试机制）
 * 4. FFmpeg 转码启动（使用 StreamStartValidator）
 */
class SmartStreamProcessor {
public:
  /**
   * @brief 构造函数
   * @param zlm_client ZLM 客户端
   * @param stream_manager 流管理器
   * @param process_manager 进程管理器（可选）
   * @param stream_info_detector 流信息检测器（可选）
   * @param stream_start_validator 流启动验证器（可选）
   * @param config 配置对象（可选，用于读取超时和重试参数）
   */
  SmartStreamProcessor(
      std::shared_ptr<streaming::ZLMClient> zlm_client,
      std::shared_ptr<streaming::StreamManager> stream_manager,
      std::shared_ptr<process::ProcessManager> process_manager = nullptr,
      std::unique_ptr<StreamInfoDetector> stream_info_detector = nullptr,
      std::unique_ptr<StreamStartValidator> stream_start_validator = nullptr,
      std::shared_ptr<config::Config> config = nullptr);

  /**
   * @brief 处理流启动（智能决策：直接代理 or FFmpeg 转码）
   *
   * @param source_url 源流 URL
   * @param target_app 目标应用名
   * @param target_stream 目标流名
   * @param output_protocol 输出协议（http-flv, hls, webrtc 等）
   * @param protocol 输入协议（rtsp, http-flv, hls, rtmp 等，用于 StreamManager
   * 注册）
   * @param gateway_type Gateway 类型（用于日志和状态管理）
   * @param direct_proxy_callback 直接代理回调（调用 ZLM API）
   * @param get_stream_info_callback 获取流信息回调（用于验证流是否活跃）
   * @param start_ffmpeg_callback 启动 FFmpeg 回调（如果需要转码）
   * @param read_ffmpeg_error_callback 读取 FFmpeg 错误日志回调
   * @param stream_schema 流协议 schema（用于 GetStreamInfo，默认为空，使用
   * protocol）
   * @param detect_timeout 流信息检测超时（秒，默认20秒）
   * @param detect_username 流信息检测用户名（可选，用于需要认证的流）
   * @param detect_password 流信息检测密码（可选，用于需要认证的流）
   * @return 流处理结果
   */
  StreamProcessResult
  ProcessStream(const std::string &source_url, const std::string &target_app,
                const std::string &target_stream,
                const std::string &output_protocol, const std::string &protocol,
                const std::string &gateway_type,
                DirectProxyCallback direct_proxy_callback,
                GetStreamInfoCallback get_stream_info_callback,
                StartFFmpegCallback start_ffmpeg_callback,
                ReadFFmpegErrorCallback read_ffmpeg_error_callback,
                const std::string &stream_schema = "", int detect_timeout = 20,
                const std::string &detect_username = "",
                const std::string &detect_password = "");

  /**
   * @brief 注册流到 StreamManager（成功时调用）
   */
  void RegisterStream(const std::string &target_app,
                      const std::string &target_stream,
                      const std::string &protocol,
                      const std::string &output_protocol,
                      const std::string &source_url,
                      const std::string &gateway_type, GatewayStatus status,
                      int pid, const std::string &processing_type = "0");

  /**
   * @brief 报告流创建结果到 StreamManager
   */
  void ReportStreamResult(const std::string &target_app,
                          const std::string &target_stream, bool success,
                          const std::string &error_code = "",
                          const std::string &error_message = "");

private:
  /**
   * @brief 检测流信息
   */
  StreamInfoResult DetectStreamInfo(const std::string &source_url,
                                    int timeout_seconds,
                                    const std::string &username = "",
                                    const std::string &password = "");

  /**
   * @brief 智能转码决策
   *
   * @param stream_info_result 检测到的流信息
   * @param protocol 协议类型（用于判断是否跳过直接代理）
   * @param use_ffmpeg 输出：是否使用 FFmpeg
   * @param video_only_transcode 输出：是否只转码视频
   */
  void MakeTranscodingDecision(const StreamInfoResult &stream_info_result,
                               const std::string &protocol,
                               const std::string &output_protocol,
                               bool &use_ffmpeg, bool &video_only_transcode);

  /**
   * @brief 直接代理结果
   */
  enum class DirectProxyResult {
    Success,        // 成功
    TransientError, // 临时错误（网络波动等，可以重试）
    PermanentError, // 永久错误（源流不存在、认证失败等，不应重试）
    Timeout // 超时（可以重试，但需要更多时间）
  };

  /**
   * @brief 尝试直接代理并验证（改进版本，区分错误类型）
   *
   * @param target_app 目标应用名
   * @param target_stream 目标流名
   * @param source_url 源流 URL
   * @param direct_proxy_callback 直接代理回调
   * @param get_stream_info_callback 获取流信息回调
   * @param stream_schema 流协议 schema
   * @param error_code 输出：错误码（如果失败）
   * @param error_message 输出：错误消息（如果失败）
   * @return 直接代理结果
   */
  DirectProxyResult TryDirectProxy(
      const std::string &target_app, const std::string &target_stream,
      const std::string &source_url, DirectProxyCallback direct_proxy_callback,
      GetStreamInfoCallback get_stream_info_callback,
      const std::string &stream_schema, std::string &error_code,
      std::string &error_message);

  /**
   * @brief 启动 FFmpeg 转码
   *
   * @param target_app 目标应用名
   * @param target_stream 目标流名
   * @param stream_info_result 流信息检测结果
   * @param start_ffmpeg_callback 启动 FFmpeg 回调
   * @param read_ffmpeg_error_callback 读取错误日志回调
   * @param pid 输出：进程ID
   * @param status 输出：状态
   * @return 是否成功
   */
  bool StartFFmpegTranscode(const std::string &target_app,
                            const std::string &target_stream,
                            const StreamInfoResult &stream_info_result,
                            StartFFmpegCallback start_ffmpeg_callback,
                            ReadFFmpegErrorCallback read_ffmpeg_error_callback,
                            GetStreamInfoCallback get_stream_info_callback,
                            int &pid, GatewayStatus &status);

private:
  std::shared_ptr<streaming::ZLMClient> zlm_client_;
  std::shared_ptr<streaming::StreamManager> stream_manager_;
  std::shared_ptr<process::ProcessManager> process_manager_;
  std::unique_ptr<StreamInfoDetector> stream_info_detector_;
  std::unique_ptr<StreamStartValidator> stream_start_validator_;
  std::shared_ptr<config::Config> config_;

  // 直接代理验证参数（从配置读取，如果没有配置则使用默认值）
  int max_retries_ = 5;
  int retry_interval_ms_ = 2000;
};

} // namespace utils
} // namespace gateway

#endif // GATEWAY_UTILS_SMART_STREAM_PROCESSOR_HPP
