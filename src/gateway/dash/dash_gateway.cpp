#include "gateway/dash/dash_gateway.hpp"
#include "config/config_loader.hpp"
#include "gateway/utils/bitrate_allocation_helper.hpp"
#include "gateway/utils/error_codes.hpp"
#include "gateway/utils/error_inference.hpp"
#include "gateway/utils/ffmpeg_process_helper.hpp"
#include "gateway/utils/gateway_config_helper.hpp"
#include "gateway/utils/smart_stream_processor.hpp"
#include "process/ffmpeg_executor.hpp"
#include "process/process_manager.hpp"
#include "process/process_monitor.hpp"
#include "streaming/stream_manager.hpp"
#include "streaming/zlmediakit/zlm_client.hpp"
#include "utils/bitrate_allocator.hpp"
#include "utils/ffmpeg_params.hpp"
#include "utils/logger.hpp"
#include "utils/zlm_url_builder.hpp"
#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <errno.h>
#include <fcntl.h>
#include <fstream>
#include <signal.h>
#include <sstream>
#include <sys/wait.h>
#include <thread>
#include <unistd.h>

#include "gateway/utils/gateway_registry.hpp"

namespace gateway {

namespace {
static gateway::utils::AutoRegister
    reg("dash",
        [](const gateway::utils::GatewayContext &ctx)
            -> std::shared_ptr<gateway::GatewayBase> {
          if (ctx.config->dash.enabled && ctx.config->rtsp.enabled) {
            return std::make_shared<DASHGateway>(
                ctx.config, ctx.zlm_client, ctx.process_manager,
                ctx.stream_manager, ctx.bitrate_allocator);
          }
          return nullptr;
        });
} // namespace

DASHGateway::DASHGateway(
    std::shared_ptr<config::Config> config,
    std::shared_ptr<streaming::ZLMClient> zlm_client,
    std::shared_ptr<process::ProcessManager> process_manager,
    std::shared_ptr<streaming::StreamManager> stream_manager,
    std::shared_ptr<::utils::BitrateAllocator> bitrate_allocator)
    : config_(config), zlm_client_(zlm_client),
      process_manager_(process_manager), stream_manager_(stream_manager) {
  // 使用配置辅助工具获取路径
  ffmpeg_path_ = gateway::utils::GatewayConfigHelper::GetFFmpegPath(config_);
  std::string ffprobe_path =
      gateway::utils::GatewayConfigHelper::GetFFprobePath(config_);
  zlm_rtsp_url_ = gateway::utils::GatewayConfigHelper::BuildZLMRTSPURL(config_);

  // 初始化通用辅助类
  // 1. FFmpeg 进程管理辅助
  ffmpeg_helper_ =
      std::make_unique<gateway::utils::FFmpegProcessHelper>(process_manager_);

  // 2. 码率分配辅助
  bitrate_helper_ = std::make_unique<gateway::utils::BitrateAllocationHelper>(
      bitrate_allocator);

  // 3. 智能流处理器
  auto stream_info_detector =
      std::make_unique<gateway::utils::StreamInfoDetector>(ffprobe_path);

  gateway::utils::StreamStartValidationConfig validator_config;
  // 从配置中读取流验证参数，如果没有配置则使用默认值
  if (config_) {
    validator_config.process_stable_wait_ms =
        config_->gateway.stream_validation.process_stable_wait_ms;
    validator_config.zlm_check_interval_ms =
        config_->gateway.stream_validation.zlm_check_interval_ms;
    validator_config.zlm_check_timeout_ms =
        config_->gateway.stream_validation.zlm_check_timeout_ms;
    validator_config.max_check_attempts =
        config_->gateway.stream_validation.max_check_attempts;
  } else {
    // 默认值（向后兼容）
    validator_config.process_stable_wait_ms = 0;
    validator_config.zlm_check_interval_ms = 500;
    validator_config.zlm_check_timeout_ms = 5000;
    validator_config.max_check_attempts = 10;
  }

  std::unique_ptr<gateway::utils::StreamStartValidator> stream_start_validator =
      nullptr;
  if (zlm_client_ && process_manager_) {
    stream_start_validator =
        std::make_unique<gateway::utils::StreamStartValidator>(
            zlm_client_, process_manager_, validator_config);
  }

  smart_processor_ = std::make_unique<gateway::utils::SmartStreamProcessor>(
      zlm_client_, stream_manager_, process_manager_,
      std::move(stream_info_detector), std::move(stream_start_validator),
      config_);
}

DASHGateway::~DASHGateway() {
  // 停止所有流
  GatewayBase::StopAllStreamsWithFFmpeg<StreamInfo>(
      streams_, streams_mutex_, [this](StreamInfo &info) {
        std::string stream_id =
            GenerateStreamId(info.target_app, info.target_stream);
        ffmpeg_helper_->StopProcess(
            info.pid, stream_id,
            [&info](GatewayStatus status) { info.status = status; });
      });
}

Result<void> DASHGateway::Start(const std::string &source_url,
                                const std::string &target_app,
                                const std::string &target_stream,
                                const std::string &output_protocol) {
  std::string stream_id = GenerateStreamId(target_app, target_stream);

  // 检查流是否已存在
  auto [exists, is_running] = GatewayBase::CheckStreamExists<StreamInfo>(
      stream_id, streams_, streams_mutex_, target_app, target_stream,
      [this](StreamInfo &info) {
        std::string stream_id =
            GenerateStreamId(info.target_app, info.target_stream);
        ffmpeg_helper_->StopProcess(
            info.pid, stream_id,
            [&info](GatewayStatus status) { info.status = status; });
      });

  if (exists && is_running) {
    return Result<void>::Success();
  }

  std::lock_guard<std::mutex> lock(
      streams_mutex_); // CheckStreamExists已经释放锁，这里重新加锁

  // 创建新的流信息
  StreamInfo info;
  info.source_url = source_url;
  info.target_app = target_app;
  info.target_stream = target_stream;
  info.output_protocol = output_protocol; // 保存输出协议
  info.status = GatewayStatus::Starting;

  std::string resolved_output =
      info.output_protocol.empty() ? "dash" : info.output_protocol;

  // DASH Gateway 总是使用 FFmpeg 转码（不支持直接代理）
  // 使用 SmartStreamProcessor 处理流启动
  auto result = smart_processor_->ProcessStream(
      source_url, target_app, target_stream, resolved_output, "dash",
      "dash_gateway",
      // 直接代理回调（DASH 不支持直接代理）
      [](const std::string &app, const std::string &stream,
         const std::string &url) {
        return false; // DASH 不支持直接代理，总是使用 FFmpeg
      },
      // 获取流信息回调
      [this](const std::string &app, const std::string &stream,
             const std::string &schema) {
        return zlm_client_ ? zlm_client_->GetStreamInfo(
                                 app, stream, schema.empty() ? "dash" : schema)
                           : streaming::StreamInfo();
      },
      // 启动 FFmpeg 回调
      [this, &info, stream_id, resolved_output](
          const gateway::utils::StreamInfoResult &stream_info_result) {
        // 获取智能分配的码率
        // 获取智能分配的码率（考虑源流码率，不降级）
        int source_bitrate_kbps = stream_info_result.total_bitrate > 0
                                      ? stream_info_result.total_bitrate / 1000
                                      : 0;
        int recommended_bitrate = bitrate_helper_->GetRecommendedBitrate(
            info.target_app, info.target_stream, "dash", resolved_output,
            stream_info_result.video_width > 0
                ? std::to_string(stream_info_result.video_width) + "x" +
                      std::to_string(stream_info_result.video_height)
                : "1280x720",
            static_cast<int>(stream_info_result.video_fps > 0.0
                                 ? stream_info_result.video_fps
                                 : 30),
            5, // priority
            source_bitrate_kbps);

        // 尝试使用优化命令（基于流信息检测）
        std::string command = BuildOptimizedFFmpegCommand(
            info, stream_info_result, recommended_bitrate);
        if (command.empty()) {
          command =
              BuildFFmpegCommand(info, stream_info_result, recommended_bitrate);
        }

        int pid = 0;
        GatewayStatus status = GatewayStatus::Stopped;

        std::string log_file = "/tmp/ffmpeg_dash_" + info.target_app + "_" +
                               info.target_stream + ".log";
        if (ffmpeg_helper_->StartProcess(command, stream_id, log_file, pid,
                                         status)) {
          return pid;
        }
        return 0;
      },
      // 读取错误日志回调
      [this, target_app, target_stream]() {
        std::string stream_id = GenerateStreamId(target_app, target_stream);
        return ffmpeg_helper_->ReadErrorLog(stream_id);
      },
      "dash" // stream_schema
  );

  // 更新流信息
  info.pid = result.pid;
  info.status = result.status;

  if (result.success) {
    streams_[stream_id] = info;
    return Result<void>::Success();
  } else {
    // 错误已在 SmartStreamProcessor 中处理
    info.status = GatewayStatus::Error;
    streams_[stream_id] = info;
    return Result<void>::Failure(
        gateway::InternalServerException(result.error_message));
  }
}

Result<void> DASHGateway::Stop(const std::string &target_app,
                               const std::string &target_stream) {
  std::string stream_id = GenerateStreamId(target_app, target_stream);
  return GatewayBase::StopWithFFmpeg<StreamInfo>(
      stream_id, target_app, target_stream, streams_, streams_mutex_,
      [this](StreamInfo &info) {
        std::string stream_id =
            GenerateStreamId(info.target_app, info.target_stream);
        return ffmpeg_helper_->StopProcess(
            info.pid, stream_id,
            [&info](GatewayStatus status) { info.status = status; });
      },
      nullptr, // DASH Gateway 不需要删除 ZLM 流
      "DASH");
}

bool DASHGateway::IsRunning(const std::string &target_app,
                            const std::string &target_stream) {
  std::string stream_id = GenerateStreamId(target_app, target_stream);

  std::lock_guard<std::mutex> lock(streams_mutex_);

  auto it = streams_.find(stream_id);
  if (it == streams_.end()) {
    return false;
  }

  // 检查进程是否还在运行
  if (it->second.pid > 0 &&
      process::ProcessMonitor::IsProcessAlive(it->second.pid)) {
    it->second.status = GatewayStatus::Running;
    return true;
  } else {
    it->second.status = GatewayStatus::Stopped;
    return false;
  }
}

GatewayStatus DASHGateway::GetStatus(const std::string &target_app,
                                     const std::string &target_stream) {
  std::string stream_id = GenerateStreamId(target_app, target_stream);

  std::lock_guard<std::mutex> lock(streams_mutex_);

  auto it = streams_.find(stream_id);
  if (it == streams_.end()) {
    return GatewayStatus::Stopped;
  }

  // 更新状态
  if (it->second.pid > 0 &&
      process::ProcessMonitor::IsProcessAlive(it->second.pid)) {
    it->second.status = GatewayStatus::Running;
  } else {
    it->second.status = GatewayStatus::Stopped;
  }

  return it->second.status;
}

std::string DASHGateway::BuildFFmpegCommand(
    const StreamInfo &info,
    const gateway::utils::StreamInfoResult &stream_info_result,
    int bitrate_kbps) {
  std::ostringstream oss;

  // 处理 file:// 协议，转换为直接文件路径
  std::string source_url = info.source_url;
  if (source_url.find("file://") == 0) {
    source_url = source_url.substr(7); // 移除 "file://" 前缀
  }

  // FFmpeg 命令：从 DASH 源拉流并推送到 ZLMediaKit
  // 优化：减小分析时长和探测大小以降低延迟
  oss << ffmpeg_path_ << " -re" // 实时读取，按输入流的帧率读取
      << " -stream_loop -1"     // 无限循环播放（用于静态 DASH 源）
      << " -analyzeduration 1000000" // 分析时长：1秒（降低延迟）
      << " -probesize 1000000"       // 探测大小：1MB（降低延迟）
      << " -fflags +genpts+igndts"   // 生成 PTS，忽略 DTS
      << " -flags low_delay"         // 低延迟标志
      << " -strict experimental"     // 允许实验性功能
      << " -i \"" << source_url << "\""; // 输入源（DASH manifest.mpd）

  // 根据输出协议决定转码参数
  if (info.output_protocol == "webrtc") {
    // 使用检测结果的分辨率
    ::utils::FFmpegParams::AddWebRTCEncodingParams(
        oss, bitrate_kbps, false, stream_info_result.video_width,
        stream_info_result.video_height);
    int final_bitrate = (bitrate_kbps > 0) ? bitrate_kbps : 1200;
    LOG_DEBUG("DASH Gateway: 使用 WebRTC 兼容转码参数，码率: {}k",
              final_bitrate);
    // WebRTC 模式：推 RTMP/FLV 到 ZLM，与 RTSP Gateway 保持一致，避免兼容性问题
    oss << " -f flv" << " \""
        << ::utils::zlm_url_builder::BuildRTMPUrlWithSecret(
               config_, info.target_app, info.target_stream, "DASH Gateway")
        << "\"";
    return oss.str();
  } else if (info.output_protocol == "http-flv" ||
             info.output_protocol == "hls" || info.output_protocol == "rtsp") {
    // HTTP-FLV/HLS/RTSP 模式：统一推 RTMP/FLV 到 ZLM
    // 使用检测结果的分辨率
    ::utils::FFmpegParams::AddStreamingEncodingParams(
        oss, bitrate_kbps, stream_info_result.video_width,
        stream_info_result.video_height, info.output_protocol);
    int final_bitrate = (bitrate_kbps > 0) ? bitrate_kbps : 1800;
    int gop_size = (info.output_protocol == "hls") ? 12 : 18;
    LOG_DEBUG(
        "DASH Gateway: 使用优化转码参数 (分辨率: {}x{}, {}k, GOP={})",
        stream_info_result.video_width > 0 ? stream_info_result.video_width
                                           : 1280,
        stream_info_result.video_height > 0 ? stream_info_result.video_height
                                            : 720,
        final_bitrate, gop_size);
    oss << " -f flv" << " \""
        << ::utils::zlm_url_builder::BuildRTMPUrlWithSecret(
               config_, info.target_app, info.target_stream, "DASH Gateway")
        << "\"";
    return oss.str();
  } else {
    // 默认模式：统一推 RTMP/FLV 到 ZLM
    oss << " -c:v copy";
    ::utils::FFmpegParams::AddRateLimitParams(oss, bitrate_kbps);
    oss << " -c:a copy" << " -f flv" << " \""
        << ::utils::zlm_url_builder::BuildRTMPUrlWithSecret(
               config_, info.target_app, info.target_stream, "DASH Gateway")
        << "\"";
    return oss.str();
  }
}

std::string DASHGateway::BuildOptimizedFFmpegCommand(
    const StreamInfo &info,
    const gateway::utils::StreamInfoResult &stream_info_result,
    int bitrate_kbps) {

  // 如果输出协议是 WebRTC，根据兼容性检查决定转码策略
  if (info.output_protocol == "webrtc") {
    std::ostringstream oss;
    oss << ffmpeg_path_ << " -fflags +genpts+igndts"
        << " -analyzeduration 20000000" << " -probesize 20000000" << " -i \""
        << info.source_url << "\"";

    std::string reason;
    bool compatible = gateway::utils::StreamInfoDetector::IsWebRTCCompatible(
        stream_info_result, config_->gateway.webrtc_compat, reason);

    if (compatible) {
      LOG_INFO(
          "[DASHGateway] WebRTC compatible stream detected, using copy: {}",
          info.source_url);
      oss << " -c:v copy -c:a copy";
    } else {
      // Check if only audio is incompatible (Video, Profile, Pixel are NOT in
      // reason)
      if (reason.find("Video") == std::string::npos &&
          reason.find("Profile") == std::string::npos &&
          reason.find("Pixel") == std::string::npos &&
          reason.find("Audio") != std::string::npos) {
        // Video is fine, transcode audio
        LOG_INFO(
            "[DASHGateway] WebRTC video compatible, transcoding audio only: {}",
            info.source_url);
        oss << " -c:v copy";
        // Audio params aligned with AddWebRTCEncodingParams
        oss << " -c:a aac -b:a 128k -ar 48000 -ac 2";
      } else {
        // Full transcoding required
        LOG_DEBUG("[DASHGateway] WebRTC incompatible ({}), forcing full "
                  "transcode: {}",
                  reason, info.source_url);
        ::utils::FFmpegParams::AddWebRTCEncodingParams(
            oss, bitrate_kbps, false, stream_info_result.video_width,
            stream_info_result.video_height);
      }
    }

    // WebRTC 模式：推 RTMP/FLV 到 ZLM，与 RTSP Gateway 保持一致，避免兼容性问题
    oss << " -f flv" << " \""
        << ::utils::zlm_url_builder::BuildRTMPUrlWithSecret(
               config_, info.target_app, info.target_stream, "DASH Gateway")
        << "\"";
    return oss.str();
  }
  if (!stream_info_result.valid) {
    LOG_DEBUG("[DASHGateway] StreamInfoDetector 检测失败，使用默认参数: {}",
              info.source_url);
    return "";
  }

  LOG_INFO(
      "[DASH Gateway] Detected stream info - Audio: {} ({}Hz, {}ch, {}kbps), "
      "Video: {} ({}x{}@{}fps, {}kbps), Total: {}kbps",
      stream_info_result.audio_codec.empty() ? "(none)"
                                             : stream_info_result.audio_codec,
      stream_info_result.audio_sample_rate > 0
          ? stream_info_result.audio_sample_rate
          : 0,
      stream_info_result.audio_channels > 0 ? stream_info_result.audio_channels
                                            : 0,
      stream_info_result.audio_bitrate > 0
          ? stream_info_result.audio_bitrate / 1000
          : 0,
      stream_info_result.video_codec.empty() ? "(none)"
                                             : stream_info_result.video_codec,
      stream_info_result.video_width, stream_info_result.video_height,
      stream_info_result.video_fps > 0.0 ? stream_info_result.video_fps : 0.0,
      stream_info_result.video_bitrate > 0
          ? stream_info_result.video_bitrate / 1000
          : 0,
      stream_info_result.total_bitrate > 0
          ? stream_info_result.total_bitrate / 1000
          : 0);

  // 检查是否可以使用 -c copy 或只转码音频
  bool can_use_copy =
      gateway::utils::StreamInfoDetector::CanUseFullCopy(stream_info_result);
  bool can_use_video_copy =
      gateway::utils::StreamInfoDetector::CanUseVideoCopy(stream_info_result);

  std::ostringstream oss;
  oss << ffmpeg_path_ << " -re" // 实时读取
      << " -stream_loop -1"     // 无限循环播放（用于静态 DASH 源）
      << " -fflags +genpts+igndts"   // 生成 PTS，忽略 DTS
      << " -flags low_delay"         // 低延迟标志
      << " -strict experimental"     // 允许实验性功能
      << " -analyzeduration 1000000" // 分析时长：1秒（降低延迟）
      << " -probesize 1000000";      // 探测大小：1MB（降低延迟）

  // 如果可以使用 copy，使用优化的 copy 参数
  if (can_use_copy) {
    oss << " -i \"" << info.source_url << "\""
        << " -c:v copy -c:a copy"; // 使用 copy，性能最优
    LOG_DEBUG("[DASHGateway] 使用 -c copy（编码兼容）: {}", info.source_url);
    // Copy 模式：统一推 RTMP/FLV 到 ZLM
    oss << " -f flv" << " \""
        << ::utils::zlm_url_builder::BuildRTMPUrlWithSecret(
               config_, info.target_app, info.target_stream, "DASH Gateway")
        << "\"";
    return oss.str();
  } else {
    // 需要转码
    oss << " -i \"" << info.source_url << "\"";

    if (info.output_protocol == "http-flv" || info.output_protocol == "hls" ||
        info.output_protocol == "rtsp") {
      // HTTP-FLV/HLS/RTSP 模式：智能转码决策
      // 如果视频兼容但音频不兼容，只转码音频（-c:v copy -c:a aac）
      if (can_use_video_copy) {
        // 视频兼容，只转码音频
        oss << " -c:v copy"; // 视频使用 copy，保持质量
        // 智能分辨率处理：保持源流分辨率（不添加 -vf scale）
        if (stream_info_result.video_width > 0 &&
            stream_info_result.video_height > 0) {
          LOG_INFO("[DASHGateway] "
                   "使用智能转码参数（只转码音频），保持源流分辨率 {}x{}: {}",
                   stream_info_result.video_width,
                   stream_info_result.video_height, info.source_url);
        } else {
          LOG_DEBUG("[DASHGateway] 使用智能转码参数（只转码音频）: {}",
                    info.source_url);
        }
        // 音频转码参数
        oss << " -c:a aac -b:a 128k -ar 44100 -ac 2";
      } else {
        // 视频不兼容，需要转码视频和音频
        // 智能分辨率处理：保持源流分辨率
        ::utils::FFmpegParams::AddStreamingEncodingParams(
            oss, bitrate_kbps, stream_info_result.video_width,
            stream_info_result.video_height, info.output_protocol);
        oss << " -fpsmax 30"; // 限制最大帧率
        if (stream_info_result.video_width > 0 &&
            stream_info_result.video_height > 0) {
          LOG_INFO("[DASHGateway] 使用转码参数，保持源流分辨率 {}x{}: {}",
                   stream_info_result.video_width,
                   stream_info_result.video_height, info.source_url);
        } else {
          LOG_DEBUG("[DASHGateway] 使用转码参数: {}", info.source_url);
        }
      }
    } else {
      // 使用检测到的编码参数
      // 视频编码参数
      if (!stream_info_result.video_codec.empty()) {
        if (gateway::utils::StreamInfoDetector::IsVideoCodecCompatible(
                stream_info_result.video_codec)) {
          oss << " -c:v copy"; // 视频兼容，使用 copy
        } else {
          oss << " -c:v libx264"; // 视频不兼容，转码为 H.264
          if (bitrate_kbps > 0) {
            ::utils::FFmpegParams::AddVideoBitrateParams(oss, bitrate_kbps);
          } else if (stream_info_result.video_bitrate > 0) {
            oss << " -b:v " << stream_info_result.video_bitrate;
          }
          // 保持原始分辨率：不添加 -vf scale，让 FFmpeg 保持源流分辨率
          // 注意：如果源流分辨率与目标不匹配，FFmpeg 会自动处理
          // 但我们不强制缩放，保持源流质量
          // 保持原始帧率
          if (stream_info_result.video_fps > 0) {
            oss << " -r " << static_cast<int>(stream_info_result.video_fps);
          }
        }
      }

      // 音频编码参数
      if (!stream_info_result.audio_codec.empty()) {
        if (gateway::utils::StreamInfoDetector::IsAudioCodecCompatible(
                stream_info_result.audio_codec)) {
          oss << " -c:a copy"; // 音频兼容，使用 copy
        } else {
          oss << " -c:a aac -b:a 128k -ar 44100 -ac 2"; // 音频不兼容，转码为
                                                        // AAC
        }
      } else {
        oss << " -c:a aac -b:a 128k -ar 44100 -ac 2"; // 默认转码为 AAC
      }
      LOG_DEBUG("[DASHGateway] 使用转码参数（基于检测结果）: {}",
                info.source_url);
    }
    // 统一推 RTMP/FLV 到 ZLM
    oss << " -f flv" << " \""
        << ::utils::zlm_url_builder::BuildRTMPUrlWithSecret(
               config_, info.target_app, info.target_stream, "DASH Gateway")
        << "\"";
    return oss.str();
  }
}

} // namespace gateway
