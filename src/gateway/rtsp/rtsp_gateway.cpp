#include "gateway/rtsp/rtsp_gateway.hpp"
#include "gateway/utils/ffmpeg_command_builder.hpp"
#include "gateway/utils/smart_stream_processor.hpp"
#include "gateway/utils/ffmpeg_process_helper.hpp"
#include "gateway/utils/bitrate_allocation_helper.hpp"
#include "gateway/utils/error_codes.hpp"
#include "config/config_loader.hpp"
#include "streaming/zlmediakit/zlm_client.hpp"
#include "streaming/stream_manager.hpp"
#include "process/process_manager.hpp"
#include "process/process_monitor.hpp"
#include "utils/logger.hpp"
#include "utils/stream_status_checker.hpp"
#include "utils/ffmpeg_params.hpp"
#include "gateway/utils/stream_info_detector.hpp"

#include <sstream>
#include <algorithm>
#include <thread>
#include <chrono>


#include "gateway/utils/gateway_registry.hpp"

namespace gateway {

namespace {
static gateway::utils::AutoRegister reg("rtsp", [](const gateway::utils::GatewayContext& ctx) -> std::shared_ptr<gateway::GatewayBase> {
    if (ctx.config->rtsp.enabled) {
        return std::make_shared<RTSPGateway>(
            ctx.config, ctx.zlm_client, ctx.process_manager, ctx.stream_manager, ctx.bitrate_allocator);
    }
    return nullptr;
});
}

RTSPGateway::RTSPGateway(std::shared_ptr<config::Config> config,
                         std::shared_ptr<streaming::ZLMClient> zlm_client,
                         std::shared_ptr<process::ProcessManager> process_manager,
                         std::shared_ptr<streaming::StreamManager> stream_manager,
                         std::shared_ptr<::utils::BitrateAllocator> bitrate_allocator)
    : config_(config), zlm_client_(zlm_client), process_manager_(process_manager), 
      stream_manager_(stream_manager) {
    
    ffmpeg_path_ = config_->rtsp.ffmpeg_path;
    if (ffmpeg_path_.empty()) {
        ffmpeg_path_ = "ffmpeg";
    }

    ffprobe_path_ = config_->rtsp.ffprobe_path;
    if (ffprobe_path_.empty()) {
        ffprobe_path_ = "ffprobe";
    }

    // 构建 ZLMediaKit RTSP 推流地址
    // 注意：FFmpeg 推流到 ZLM 通常使用 RTSP 或 RTMP。
    // 这里 ZLM 接收通常是 RTMP 推流（默认端口 1935）。
    // RTSP Gateway 实际上是将外部 RTSP 流转码/转发 推送到 ZLM，通常推 RTMP。
    // zlm_rtsp_url_ 这个变量名可能有点误导，实际上我们构建的是 RTMP URL 在 helper 中。
    // 这里我们初始化一个 RTMP URL base
    std::ostringstream oss;
    oss << "rtmp://127.0.0.1:" << config_->zlmediakit.rtmp_port;
    zlm_rtsp_url_ = oss.str();

    // 1. FFmpeg 进程管理辅助
    ffmpeg_helper_ = std::make_unique<gateway::utils::FFmpegProcessHelper>(process_manager_);

    // 2. 码率分配辅助
    bitrate_helper_ = std::make_unique<gateway::utils::BitrateAllocationHelper>(bitrate_allocator);

    // 3. 统一 FFmpeg 命令构建器
    // 注意：这里传入的 zlm_rtsp_url_ 其实是 RTMP base URL
    ffmpeg_builder_ = std::make_unique<gateway::utils::FFmpegCommandBuilder>(
        config_, ffmpeg_path_, zlm_rtsp_url_);

    // 4. 智能流处理器
    auto stream_info_detector = std::make_unique<gateway::utils::StreamInfoDetector>(ffprobe_path_);

    gateway::utils::StreamStartValidationConfig validator_config;
    // 使用通用流验证配置
    if (config_) {
        validator_config.process_stable_wait_ms = config_->gateway.stream_validation.process_stable_wait_ms;
        validator_config.zlm_check_interval_ms = config_->gateway.stream_validation.zlm_check_interval_ms;
        validator_config.zlm_check_timeout_ms = config_->gateway.stream_validation.zlm_check_timeout_ms;
        validator_config.max_check_attempts = config_->gateway.stream_validation.max_check_attempts;
    }

    auto stream_start_validator = std::make_unique<gateway::utils::StreamStartValidator>(
        zlm_client_, process_manager_, validator_config);

    smart_processor_ = std::make_unique<gateway::utils::SmartStreamProcessor>(
        zlm_client_, stream_manager_, process_manager_,
        std::move(stream_info_detector),
        std::move(stream_start_validator),
        config_);

    LOG_INFO("[RTSP Gateway] 初始化完成，FFmpeg路径: {}", ffmpeg_path_);
}

RTSPGateway::~RTSPGateway() {
    std::lock_guard<std::mutex> lock(streams_mutex_);
    for (auto& pair : streams_) {
        std::string stream_id = GenerateStreamId(pair.second.target_app, pair.second.target_stream);
        if (pair.second.pid > 0) {
            ffmpeg_helper_->StopProcess(pair.second.pid, stream_id,
                [&pair](GatewayStatus status) { pair.second.status = status; });
        }
    }
    streams_.clear();
}

Result<void> RTSPGateway::Start(const std::string& source_url,
                        const std::string& target_app,
                        const std::string& target_stream,
                        const std::string& output_protocol) {
    std::string stream_id = GenerateStreamId(target_app, target_stream);
    LOG_INFO("[RTSP Gateway] AddStream Called for: {} -> {}", source_url, stream_id);
    
    // 检查是否已存在
    {
        std::lock_guard<std::mutex> lock(streams_mutex_);
        auto it = streams_.find(stream_id);
        if (it != streams_.end()) {
            if (it->second.status == GatewayStatus::Running) {
                LOG_WARN("[RTSP Gateway] 流已运行: {}/{}", target_app, target_stream);
                return Result<void>::Success();
            }
            // 如果已存在但未运行，先清理旧状态
            if (it->second.pid > 0) {
                 ffmpeg_helper_->StopProcess(it->second.pid, stream_id,
                    [&it](GatewayStatus status) { it->second.status = status; });
            }
        }
    }

    StreamInfo info;
    info.source_url = source_url;
    info.target_app = target_app;
    info.target_stream = target_stream;
    info.output_protocol = output_protocol;
    info.status = GatewayStatus::Starting;

    // 使用 SmartStreamProcessor
    auto result = smart_processor_->ProcessStream(
        source_url, target_app, target_stream, output_protocol, "rtsp", "rtsp_gateway",
        // 直接代理回调
        [this, output_protocol](const std::string& app, const std::string& stream, const std::string& url) {
            // RTSP Direct Proxy Logic
            // If output is WebRTC, we force FFmpeg to ensure strict H.264+AAC/Opus transcoding and resolution control.
            // For other outputs (FLV, HLS, RTSP), if SmartStreamProcessor detected compatibility, we use Direct Proxy (AddStreamProxy).
            // 尝试直接代理 (Direct Proxy)
            // ZLMediaKit 支持将 RTSP 直接代理并转协议为 WebRTC/HLS/FLV
            // 只有当直连失败（如编码不支持）时，SmartStreamProcessor 才会回退到 FFmpeg 转码
            return zlm_client_ && zlm_client_->AddStreamProxy(app, stream, url);
        },
        // 获取流信息回调
        [this](const std::string& app, const std::string& stream, const std::string& schema) {
            return zlm_client_ ? zlm_client_->GetStreamInfo(app, stream, schema.empty() ? "rtsp" : schema) : streaming::StreamInfo();
        },
        // 启动 FFmpeg 回调
        [this, &info, stream_id, output_protocol](const gateway::utils::StreamInfoResult& stream_info_result) {
            // 获取智能分配的码率
            int source_bitrate_kbps = stream_info_result.total_bitrate > 0 ? stream_info_result.total_bitrate / 1000 : 0;
            int recommended_bitrate = bitrate_helper_->GetRecommendedBitrate(
                info.target_app, info.target_stream, "rtsp", output_protocol,
                stream_info_result.video_width > 0 ? std::to_string(stream_info_result.video_width) + "x" + std::to_string(stream_info_result.video_height) : "1280x720",
                static_cast<int>(stream_info_result.video_fps > 0.0 ? stream_info_result.video_fps : 30),
                5,  // priority
                source_bitrate_kbps);
            
            // 使用统一构建器
            gateway::utils::FFmpegCommandOptions options;
            options.input_url = info.source_url;
            options.input_format = "rtsp";
            options.output_protocol = output_protocol;
            options.target_app = info.target_app;
            options.target_stream = info.target_stream;
            options.target_bitrate_kbps = recommended_bitrate;
            options.gateway_name = "RTSP Gateway";
            options.stream_info = stream_info_result;

            std::string command = ffmpeg_builder_->BuildCommand(options);
            
            if (output_protocol == "webrtc") {
                 LOG_INFO("[RTSP Gateway] WebRTC 模式 FFmpeg 命令: {}", command);
            } else {
                 LOG_DEBUG("[RTSP Gateway] FFmpeg 命令: {}", command);
            }
            
            int pid = 0;
            GatewayStatus status = GatewayStatus::Stopped;
            std::string log_file = "/tmp/ffmpeg_rtsp_" + info.target_app + "_" + info.target_stream + ".log";
            
            if (ffmpeg_helper_->StartProcess(command, stream_id, log_file, pid, status)) {
                return pid;
            }
            return 0;
        },
        // 读取错误日志回调
        [this, target_app, target_stream]() {
             std::string stream_id = GenerateStreamId(target_app, target_stream);
             return ffmpeg_helper_->ReadErrorLog(stream_id);
        },
        "rtsp" // schema
    );

    // 更新信息
    info.source_audio_codec = result.source_audio_codec;
    info.source_video_codec = result.source_video_codec;
    info.source_width = result.source_width;
    info.source_height = result.source_height;
    info.video_only_transcode = result.video_only_transcode;
    info.pid = result.pid;
    info.status = result.status;

    std::lock_guard<std::mutex> lock(streams_mutex_);
    if (result.success) {
        streams_[stream_id] = info;
        return Result<void>::Success();
    } else {
        info.status = GatewayStatus::Error;
        streams_[stream_id] = info;
        // Use error_message if available, otherwise generic
        std::string error_msg = "Unknown error";
        // If StreamProcessResult has specific message (it should)
        // Note: result struct definition needs checking, assuming it has members like success
        // Based on RTMP error, it might NOT have 'message'.
        // I will use generic error for now or look for error field.
        return Result<void>::Failure(gateway::InternalServerException("Failed to start RTSP stream: Process failed"));
    }
}

Result<void> RTSPGateway::Stop(const std::string& target_app,
                       const std::string& target_stream) {
    std::string stream_id = GenerateStreamId(target_app, target_stream);
    
    std::lock_guard<std::mutex> lock(streams_mutex_);
    auto it = streams_.find(stream_id);
    if (it == streams_.end()) {
        LOG_WARN("[RTSP Gateway] 停止流失败: 流不存在 {}/{}", target_app, target_stream);
        if (zlm_client_) {
            zlm_client_->DeleteStream(target_app, target_stream);
        }
        return Result<void>::Success();
    }
    
    StreamInfo& info = it->second;
    int pid = info.pid;
    
    bool success = false;
    if (pid > 0) {
        success = ffmpeg_helper_->StopProcess(pid, stream_id,
            [](GatewayStatus status) { });
    } else {
        success = true;
    }
    
    if (zlm_client_) {
        zlm_client_->DeleteStream(target_app, target_stream);
    }

    streams_.erase(it);
    
    if (success) {
        LOG_INFO("[RTSP Gateway] 停止成功: {}/{}", target_app, target_stream);
        return Result<void>::Success();
    } else {
         LOG_WARN("[RTSP Gateway] 停止流失败: {}/{}", target_app, target_stream);
         return Result<void>::Failure(gateway::InternalServerException("Failed to stop RTSP stream"));
    }
}

bool RTSPGateway::IsRunning(const std::string& target_app,
                            const std::string& target_stream) {
    std::string stream_id = GenerateStreamId(target_app, target_stream);
    std::lock_guard<std::mutex> lock(streams_mutex_);
    auto it = streams_.find(stream_id);
    if (it == streams_.end()) return false;

    StreamInfo& info = it->second;
    gateway::GatewayStatus new_status;
    int new_pid;
    bool is_running = ::utils::StreamStatusChecker::CheckIsRunning(
        info.pid, info.status, zlm_client_, target_app, target_stream, new_status, new_pid);
    
    info.status = new_status;
    info.pid = new_pid;
    return is_running;
}

GatewayStatus RTSPGateway::GetStatus(const std::string& target_app,
                                     const std::string& target_stream) {
    std::string stream_id = GenerateStreamId(target_app, target_stream);
    std::lock_guard<std::mutex> lock(streams_mutex_);
    auto it = streams_.find(stream_id);
    if (it == streams_.end()) return GatewayStatus::Stopped;

    StreamInfo& info = it->second;
    int new_pid;
    gateway::GatewayStatus new_status = ::utils::StreamStatusChecker::GetStatus(
        info.pid, info.status, zlm_client_, target_app, target_stream, new_pid);
    
    if (new_status == GatewayStatus::Error && info.status != GatewayStatus::Error) {
        LOG_WARN("[RTSP Gateway] FFmpeg 进程已退出，流状态设为 Error: {}/{} (PID: {})", 
                target_app, target_stream, info.pid);
    }
    info.status = new_status;
    info.pid = new_pid;
    return new_status;
}

} // namespace gateway
