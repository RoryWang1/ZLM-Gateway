#include "gateway/quic/quic_gateway.hpp"
#include "gateway/utils/gateway_config_helper.hpp"
#include "gateway/utils/error_codes.hpp"
#include "gateway/utils/error_inference.hpp"
#include "process/ffmpeg_executor.hpp"
#include "config/config_loader.hpp"
#include "streaming/zlmediakit/zlm_client.hpp"
#include "streaming/stream_manager.hpp"
#include "utils/bitrate_allocator.hpp"
#include "utils/ffmpeg_params.hpp"
#include "process/process_manager.hpp"
#include "process/process_monitor.hpp"
#include "utils/logger.hpp"
#include "utils/zlm_url_builder.hpp"
#include "utils/ffmpeg_log_analyzer.hpp"
#include "gateway/utils/smart_stream_processor.hpp"
#include "gateway/utils/ffmpeg_process_helper.hpp"
#include "gateway/utils/bitrate_allocation_helper.hpp"
#include "gateway/quic/quic_server.hpp"
#include "gateway/quic/fec_processor.hpp"
#include "gateway/quic/ts_parser.hpp"
#include "gateway/quic/ts_to_ffmpeg_bridge.hpp"
#include <sstream>
#include <cstdlib>
#include <unistd.h>
#include <sys/wait.h>
#include <signal.h>
#include <fcntl.h>
#include <errno.h>
#include <cstring>
#include <thread>
#include <chrono>
#include <fstream>
#include <algorithm>
#include <cctype>


#include "gateway/utils/gateway_registry.hpp"

namespace gateway {

namespace {
static gateway::utils::AutoRegister reg("quic", [](const gateway::utils::GatewayContext& ctx) -> std::shared_ptr<gateway::GatewayBase> {
    if (ctx.config->quic.enabled && ctx.config->rtsp.enabled) {
        return std::make_shared<QUICGateway>(
            ctx.config, ctx.zlm_client, ctx.process_manager, ctx.stream_manager, ctx.bitrate_allocator);
    }
    return nullptr;
});
}

QUICGateway::QUICGateway(std::shared_ptr<config::Config> config,
                         std::shared_ptr<streaming::ZLMClient> zlm_client,
                         std::shared_ptr<process::ProcessManager> process_manager,
                         std::shared_ptr<streaming::StreamManager> stream_manager,
                         std::shared_ptr<::utils::BitrateAllocator> bitrate_allocator)
    : config_(config), zlm_client_(zlm_client), process_manager_(process_manager),
      stream_manager_(stream_manager), quic_server_started_(false) {
    // 使用配置辅助工具获取路径
    ffmpeg_path_ = gateway::utils::GatewayConfigHelper::GetFFmpegPath(config_);
    std::string ffprobe_path = gateway::utils::GatewayConfigHelper::GetFFprobePath(config_);
    // 注意：QUIC Gateway 使用 RTMP 推流到 ZLM，不需要 RTSP URL

    // 初始化通用辅助类
    // 1. FFmpeg 进程管理辅助
    ffmpeg_helper_ = std::make_unique<gateway::utils::FFmpegProcessHelper>(process_manager_);
    
    // 2. 码率分配辅助
    bitrate_helper_ = std::make_unique<gateway::utils::BitrateAllocationHelper>(bitrate_allocator);
    
    // 3. 智能流处理器
    auto stream_info_detector = std::make_unique<gateway::utils::StreamInfoDetector>(ffprobe_path);
    
    gateway::utils::StreamStartValidationConfig validator_config;
    // 从配置中读取流验证参数，如果没有配置则使用默认值
    if (config_) {
        validator_config.process_stable_wait_ms = config_->gateway.stream_validation.process_stable_wait_ms;
        validator_config.zlm_check_interval_ms = config_->gateway.stream_validation.zlm_check_interval_ms;
        validator_config.zlm_check_timeout_ms = config_->gateway.stream_validation.zlm_check_timeout_ms;
        validator_config.max_check_attempts = config_->gateway.stream_validation.max_check_attempts;
    } else {
        // 默认值（向后兼容）
        validator_config.process_stable_wait_ms = 500;
        validator_config.zlm_check_interval_ms = 500;
        validator_config.zlm_check_timeout_ms = 5000;
        validator_config.max_check_attempts = 10;
    }
    
    std::unique_ptr<gateway::utils::StreamStartValidator> stream_start_validator = nullptr;
    if (zlm_client_ && process_manager_) {
        stream_start_validator = std::make_unique<gateway::utils::StreamStartValidator>(
            zlm_client_, process_manager_, validator_config);
    }
    
    smart_processor_ = std::make_unique<gateway::utils::SmartStreamProcessor>(
        zlm_client_, stream_manager_, process_manager_,
        std::move(stream_info_detector),
        std::move(stream_start_validator),
        config_);
    
    // 初始化 QUIC 服务器（如果配置启用）
    if (config_ && config_->quic.enabled) {
        std::string bind_addr = "0.0.0.0";  // 默认绑定所有接口
        uint16_t port = static_cast<uint16_t>(config_->quic.port);
        if (port == 0) port = 4433;  // 默认端口
        
        quic_server_ = std::make_unique<quic::QuicServer>(bind_addr, port);
        
        // 设置连接回调
        quic_server_->SetConnectionCallback([this](std::shared_ptr<quic::QuicConnection> conn) {
            LOG_INFO("[QUICGateway] 收到新的 QUIC 连接: connection_id={}, stream_id={}",
                    conn->GetConnectionId(), conn->GetStreamId());
            // 连接处理将在 StartQuicServerMode 中完成
        });
    }
}

QUICGateway::~QUICGateway() {
    // 停止所有流
    std::lock_guard<std::mutex> lock(streams_mutex_);
    for (auto& pair : streams_) {
        StreamInfo& info = pair.second;
        if (info.use_quic_server_mode) {
            StopQuicServerMode(info);
        } else {
            std::string stream_id = GenerateStreamId(info.target_app, info.target_stream);
            ffmpeg_helper_->StopProcess(info.pid, stream_id,
                [&info](GatewayStatus status) { info.status = status; });
        }
    }
    streams_.clear();
    
    // 停止 QUIC 服务器
    if (quic_server_) {
        quic_server_->Stop();
    }
}

Result<void> QUICGateway::Start(const std::string& source_url,
                       const std::string& target_app,
                       const std::string& target_stream,
                       const std::string& output_protocol) {
    std::string stream_id = GenerateStreamId(target_app, target_stream);
    
    // 检查流是否已存在
    auto [exists, is_running] = GatewayBase::CheckStreamExists<StreamInfo>(
        stream_id, streams_, streams_mutex_, target_app, target_stream,
        [this](StreamInfo& info) { 
            std::string stream_id = GenerateStreamId(info.target_app, info.target_stream);
            ffmpeg_helper_->StopProcess(info.pid, stream_id,
                [&info](GatewayStatus status) { info.status = status; });
        });
    
    if (exists && is_running) {
        return Result<void>::Success();
    }
    
    std::lock_guard<std::mutex> lock(streams_mutex_);  // CheckStreamExists已经释放锁，这里重新加锁

    // 创建新的流信息
    StreamInfo info;
    info.source_url = source_url;
    info.target_app = target_app;
    info.target_stream = target_stream;
    info.output_protocol = output_protocol; // 保存输出协议
    info.status = GatewayStatus::Starting;

    std::string resolved_output = info.output_protocol.empty() ? "quic" : info.output_protocol;

    // 判断使用哪种模式
    info.use_quic_server_mode = ShouldUseQuicServerMode(source_url);
    
    if (info.use_quic_server_mode) {
        // 使用 QUIC 服务器模式
        if (StartQuicServerMode(info)) {
            streams_[stream_id] = std::move(info);
            LOG_INFO("QUIC Gateway (服务器模式) 启动成功: {} -> {}/{}", 
                    source_url, target_app, target_stream);
            return Result<void>::Success();
        } else {
            info.status = GatewayStatus::Error;
            streams_[stream_id] = std::move(info);
            return Result<void>::Failure(gateway::InternalServerException("Failed to start QUIC server mode"));
        }
    }
    
    // 使用 FFmpeg 拉流模式（原有实现）
    // QUIC Gateway 总是使用 FFmpeg 转码（不支持直接代理）
    // 使用 SmartStreamProcessor 处理流启动
    auto result = smart_processor_->ProcessStream(
        source_url, target_app, target_stream, resolved_output, "quic", "quic_gateway",
        // 直接代理回调（QUIC 不支持直接代理）
        [](const std::string& app, const std::string& stream, const std::string& url) {
            return false;  // QUIC 不支持直接代理，总是使用 FFmpeg
        },
        // 获取流信息回调
        [this](const std::string& app, const std::string& stream, const std::string& schema) {
            return zlm_client_ ? zlm_client_->GetStreamInfo(app, stream, schema.empty() ? "quic" : schema) : streaming::StreamInfo();
        },
        // 启动 FFmpeg 回调
        [this, &info, stream_id, resolved_output](const gateway::utils::StreamInfoResult& stream_info_result) {
            // 获取智能分配的码率（考虑源流码率，不降级）
            int source_bitrate_kbps = stream_info_result.total_bitrate > 0 ? stream_info_result.total_bitrate / 1000 : 0;
            int recommended_bitrate = bitrate_helper_->GetRecommendedBitrate(
                info.target_app, info.target_stream, "quic", resolved_output,
                stream_info_result.video_width > 0 ? std::to_string(stream_info_result.video_width) + "x" + std::to_string(stream_info_result.video_height) : "1280x720",
                static_cast<int>(stream_info_result.video_fps > 0.0 ? stream_info_result.video_fps : 30),
                5,  // priority
                source_bitrate_kbps);
            
            // 尝试使用优化命令（基于流信息检测）
            std::string command = BuildOptimizedFFmpegCommand(info, stream_info_result, recommended_bitrate);
            if (command.empty()) {
                command = BuildFFmpegCommand(info, stream_info_result, recommended_bitrate);
            }
            
            int pid = 0;
            GatewayStatus status = GatewayStatus::Stopped;
            
            std::string log_file = "/tmp/ffmpeg_quic_" + info.target_app + "_" + info.target_stream + ".log";
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
        "quic"  // stream_schema
    );
    
    // 更新流信息
    info.pid = result.pid;
    info.status = result.status;
    
    if (result.success) {
        streams_[stream_id] = std::move(info);
        LOG_INFO("QUIC Gateway 启动成功: {} -> {}/{}", source_url, target_app, target_stream);
        return Result<void>::Success();
    } else {
        // 错误已在 SmartStreamProcessor 中处理
        info.status = GatewayStatus::Error;
        streams_[stream_id] = std::move(info);
        return Result<void>::Failure(gateway::InternalServerException(result.error_message));
    }
}

Result<void> QUICGateway::Stop(const std::string& target_app,
                       const std::string& target_stream) {
    std::string stream_id = GenerateStreamId(target_app, target_stream);
    
    std::lock_guard<std::mutex> lock(streams_mutex_);
    auto it = streams_.find(stream_id);
    if (it == streams_.end()) {
        LOG_WARN("流不存在: {}/{}", target_app, target_stream);
        return Result<void>::Success();
    }
    
    StreamInfo& info = it->second;
    bool success = false;
    
    if (info.use_quic_server_mode) {
        success = StopQuicServerMode(info);
    } else {
        std::string stream_id_inner = GenerateStreamId(info.target_app, info.target_stream);
        success = ffmpeg_helper_->StopProcess(info.pid, stream_id_inner,
            [&info](GatewayStatus status) { info.status = status; });
    }
    
    streams_.erase(it);
    
    if (success) {
        LOG_INFO("QUIC Gateway 停止成功: {}/{}", target_app, target_stream);
        return Result<void>::Success();
    } else {
        LOG_WARN("QUIC Gateway 停止失败（流可能已不存在）: {}/{}", target_app, target_stream);
        return Result<void>::Failure(gateway::InternalServerException("Failed to stop QUIC stream"));
    }
}

bool QUICGateway::IsRunning(const std::string& target_app,
                            const std::string& target_stream) {
    std::string stream_id = GenerateStreamId(target_app, target_stream);
    
    std::lock_guard<std::mutex> lock(streams_mutex_);
    
    auto it = streams_.find(stream_id);
    if (it == streams_.end()) {
        return false;
    }

    // 检查进程是否还在运行
    if (it->second.pid > 0 && process::ProcessMonitor::IsProcessAlive(it->second.pid)) {
        it->second.status = GatewayStatus::Running;
        return true;
    } else {
        it->second.status = GatewayStatus::Stopped;
        return false;
    }
}

GatewayStatus QUICGateway::GetStatus(const std::string& target_app,
                                     const std::string& target_stream) {
    std::string stream_id = GenerateStreamId(target_app, target_stream);
    
    std::lock_guard<std::mutex> lock(streams_mutex_);
    
    auto it = streams_.find(stream_id);
    if (it == streams_.end()) {
        return GatewayStatus::Stopped;
    }

    // 更新状态
    if (it->second.pid > 0 && process::ProcessMonitor::IsProcessAlive(it->second.pid)) {
        it->second.status = GatewayStatus::Running;
    } else {
        it->second.status = GatewayStatus::Stopped;
    }

    return it->second.status;
}



std::string QUICGateway::BuildFFmpegCommand(const StreamInfo& info, 
                                            const gateway::utils::StreamInfoResult& stream_info_result,
                                            int bitrate_kbps) {
    std::ostringstream oss;
    
    // FFmpeg 命令：从 QUIC 源拉流并推送到 ZLMediaKit
    // 注意：FFmpeg 对 QUIC 协议的支持可能有限，需要确保 FFmpeg 版本支持 QUIC
    // QUIC URL 格式：quic://host:port/path 或 https://host:port/path (如果服务器支持 HTTP/3)
    // 优化：减小分析时长和探测大小以降低延迟
    oss << ffmpeg_path_
        << " -re"  // 实时读取，按输入流的帧率读取
        << " -fflags +genpts+igndts"  // 生成 PTS，忽略 DTS
        << " -flags low_delay"  // 低延迟标志
        << " -strict experimental"  // 允许实验性功能
        << " -analyzeduration 1000000"  // 分析时长：1秒（降低延迟）
        << " -probesize 1000000"  // 探测大小：1MB（降低延迟）
        << " -i \"" << info.source_url << "\"";  // 输入源（QUIC）
    
    // 根据输出协议决定转码参数和推流格式
    if (info.output_protocol == "webrtc") {
        // WebRTC 模式：使用 WebRTC 优化参数，推 RTMP/FLV 到 ZLM
        // 使用检测结果的分辨率
        ::utils::FFmpegParams::AddWebRTCEncodingParams(oss, bitrate_kbps, false, 
                                                        stream_info_result.video_width, stream_info_result.video_height);
        int final_bitrate = (bitrate_kbps > 0) ? bitrate_kbps : 1200;
        LOG_DEBUG("[QUICGateway] WebRTC模式：使用 WebRTC 兼容转码参数，码率: {}k", final_bitrate);
        oss << " -f flv"
            << " \"" << ::utils::zlm_url_builder::BuildRTMPUrlWithSecret(
                config_, info.target_app, info.target_stream, "QUIC Gateway") << "\"";
        return oss.str();
    } else if (info.output_protocol == "http-flv" || info.output_protocol == "hls" || info.output_protocol == "rtsp") {
        // HTTP-FLV/HLS/RTSP 模式：统一推 RTMP/FLV 到 ZLM
        // 需要转码以确保编码兼容
        // 使用检测结果的分辨率
        ::utils::FFmpegParams::AddStreamingEncodingParams(oss, bitrate_kbps, 
                                                          stream_info_result.video_width, stream_info_result.video_height,
                                                          info.output_protocol);
        int final_bitrate = (bitrate_kbps > 0) ? bitrate_kbps : 1800;
        int gop_size = (info.output_protocol == "hls") ? 12 : 18;
        LOG_DEBUG("[QUICGateway] HTTP-FLV/HLS/RTSP模式：使用优化转码参数 (分辨率: {}x{}, {}k, GOP={})", 
                 stream_info_result.video_width > 0 ? stream_info_result.video_width : 1280,
                 stream_info_result.video_height > 0 ? stream_info_result.video_height : 720,
                 final_bitrate, gop_size);
        oss << " -f flv"
            << " \"" << ::utils::zlm_url_builder::BuildRTMPUrlWithSecret(
                config_, info.target_app, info.target_stream, "QUIC Gateway") << "\"";
        return oss.str();
    }
    
    // 默认模式：统一推 RTMP/FLV 到 ZLM
    // 即使copy模式，也要控制传输速度
    oss << " -c:v copy -c:a copy";  // 复制音视频编码，不重新编码（如果编码兼容）
    if (bitrate_kbps > 0) {
        ::utils::FFmpegParams::AddRateLimitParams(oss, bitrate_kbps);
        LOG_DEBUG("[QUICGateway] Copy模式，限制传输速度: {} kbps", bitrate_kbps);
    }
    oss << " -f flv"  // 输出格式为 FLV（统一使用 RTMP/FLV）
        << " \"" << ::utils::zlm_url_builder::BuildRTMPUrlWithSecret(
            config_, info.target_app, info.target_stream, "QUIC Gateway") << "\"";
    
    return oss.str();
}

std::string QUICGateway::BuildOptimizedFFmpegCommand(const StreamInfo& info, 
                                                     const gateway::utils::StreamInfoResult& stream_info_result,
                                                     int bitrate_kbps) {
    
    // 如果输出协议是 WebRTC，直接使用强制转码参数，但使用检测到的分辨率
    if (info.output_protocol == "webrtc") {
        std::ostringstream oss;
        oss << ffmpeg_path_
            << " -re"  // 实时读取
            << " -fflags +genpts+igndts"  // 生成 PTS，忽略 DTS
            << " -flags low_delay"  // 低延迟标志
            << " -strict experimental"  // 允许实验性功能
            << " -analyzeduration 20000000"  // 分析时长：20秒
            << " -probesize 20000000"  // 探测大小：20MB
            << " -i \"" << info.source_url << "\"";
        ::utils::FFmpegParams::AddWebRTCEncodingParams(oss, bitrate_kbps, false,
                                                       stream_info_result.video_width,
                                                       stream_info_result.video_height);
        // WebRTC 模式：推 RTMP/FLV 到 ZLM，与 RTSP Gateway 保持一致，避免兼容性问题
        oss << " -f flv"
            << " \"" << ::utils::zlm_url_builder::BuildRTMPUrlWithSecret(
                config_, info.target_app, info.target_stream, "QUIC Gateway") << "\"";
        LOG_DEBUG("[QUICGateway] (Optimized): 强制使用 WebRTC 兼容转码参数，推流格式：RTMP/FLV");
        return oss.str();
    }
    if (!stream_info_result.valid) {
        LOG_DEBUG("[QUICGateway] StreamInfoDetector 检测失败，使用默认参数: {}", info.source_url);
        return "";
    }

    LOG_INFO("[QUIC Gateway] Detected stream info - Audio: {} ({}Hz, {}ch, {}kbps), Video: {} ({}x{}@{}fps, {}kbps), Total: {}kbps", 
             stream_info_result.audio_codec.empty() ? "(none)" : stream_info_result.audio_codec,
             stream_info_result.audio_sample_rate > 0 ? stream_info_result.audio_sample_rate : 0,
             stream_info_result.audio_channels > 0 ? stream_info_result.audio_channels : 0,
             stream_info_result.audio_bitrate > 0 ? stream_info_result.audio_bitrate / 1000 : 0,
             stream_info_result.video_codec.empty() ? "(none)" : stream_info_result.video_codec,
             stream_info_result.video_width, stream_info_result.video_height,
             stream_info_result.video_fps > 0.0 ? stream_info_result.video_fps : 0.0,
             stream_info_result.video_bitrate > 0 ? stream_info_result.video_bitrate / 1000 : 0,
             stream_info_result.total_bitrate > 0 ? stream_info_result.total_bitrate / 1000 : 0);
    
    // 检查是否可以使用 -c copy
    bool can_use_copy = gateway::utils::StreamInfoDetector::CanUseFullCopy(stream_info_result);
    
    std::ostringstream oss;
    oss << ffmpeg_path_
        << " -re"  // 实时读取
        << " -fflags +genpts+igndts"  // 生成 PTS，忽略 DTS
        << " -flags low_delay"  // 低延迟标志
        << " -strict experimental"  // 允许实验性功能
        << " -analyzeduration 1000000"  // 分析时长：1秒（降低延迟）
        << " -probesize 1000000";  // 探测大小：1MB（降低延迟）
    
    // 如果可以使用 copy，使用优化的 copy 参数
    if (can_use_copy) {
        oss << " -i \"" << info.source_url << "\""
            << " -c:v copy -c:a copy";  // 使用 copy，性能最优
        // 即使 copy 模式，也要控制传输速度（统一通过 FFmpegParams）
        if (bitrate_kbps > 0) {
            ::utils::FFmpegParams::AddRateLimitParams(oss, bitrate_kbps);
            LOG_DEBUG("使用 -c copy（编码兼容），限制传输速度: {} kbps: {}", bitrate_kbps, info.source_url);
        } else {
            LOG_DEBUG("使用 -c copy（编码兼容）: {}", info.source_url);
        }
    } else {
        // 需要转码，使用检测到的编码参数
        oss << " -i \"" << info.source_url << "\"";
        
        // 视频编码参数
        if (!stream_info_result.video_codec.empty()) {
            // 检查视频编码是否兼容
            if (gateway::utils::StreamInfoDetector::IsVideoCodecCompatible(stream_info_result.video_codec)) {
                oss << " -c:v copy";  // 视频兼容，使用 copy
            } else {
                oss << " -c:v libx264";  // 视频不兼容，转码为 H.264
                // 如果提供了智能分配的码率，使用统一的码率参数工具类
                if (bitrate_kbps > 0) {
                    ::utils::FFmpegParams::AddVideoBitrateParams(oss, bitrate_kbps);
                } else if (stream_info_result.video_bitrate > 0) {
                    // 使用检测到的码率
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
            // 检查音频编码是否兼容
            if (gateway::utils::StreamInfoDetector::IsAudioCodecCompatible(stream_info_result.audio_codec)) {
                oss << " -c:a copy";  // 音频兼容，使用 copy
            } else {
                oss << " -c:a aac -b:a 128k -ar 44100 -ac 2";  // 音频不兼容，转码为 AAC
            }
        } else {
            // 没有音频，或检测失败，默认转码为 AAC
            oss << " -c:a aac -b:a 128k -ar 44100 -ac 2";
        }
        
        if (bitrate_kbps > 0) {
            LOG_DEBUG("[QUICGateway] 使用转码参数（编码不兼容），智能分配码率: {} kbps: {}", bitrate_kbps, info.source_url);
        } else {
            LOG_DEBUG("[QUICGateway] 使用转码参数（编码不兼容），使用检测到的参数: {}", info.source_url);
        }
    }
    
    // 统一推 RTMP/FLV 到 ZLM
    oss << " -f flv"  // 输出格式为 FLV（统一使用 RTMP/FLV）
        << " \"" << ::utils::zlm_url_builder::BuildRTMPUrlWithSecret(
            config_, info.target_app, info.target_stream, "QUIC Gateway") << "\"";
    
    return oss.str();
}

bool QUICGateway::ShouldUseQuicServerMode(const std::string& source_url) const {
    // 判断 URL 格式：quic://host:port/stream_id 表示使用 QUIC 服务器模式
    // 其他格式（如 https:// 或 http://）使用 FFmpeg 拉流模式
    return source_url.find("quic://") == 0;
}

bool QUICGateway::StartQuicServerMode(StreamInfo& info) {
    // 确保 QUIC 服务器已启动
    {
        std::lock_guard<std::mutex> lock(quic_server_mutex_);
        if (!quic_server_) {
            LOG_ERROR("[QUICGateway] QUIC 服务器未初始化");
            return false;
        }
        
        if (!quic_server_started_) {
            if (!quic_server_->Start()) {
                LOG_ERROR("[QUICGateway] 启动 QUIC 服务器失败");
                return false;
            }
            quic_server_started_ = true;
        }
    }
    
    // 解析 source_url 获取流 ID
    // 格式：quic://host:port/stream_id
    std::string stream_id_from_url;
    size_t last_slash = info.source_url.find_last_of('/');
    if (last_slash != std::string::npos && last_slash + 1 < info.source_url.length()) {
        stream_id_from_url = info.source_url.substr(last_slash + 1);
    }
    
    // 创建 FEC 处理器
    std::map<std::string, int> fec_params;
    fec_params["L"] = 10;  // 默认值，应该从配置读取
    fec_params["D"] = 10;  // 默认值，应该从配置读取
    info.fec_processor = std::make_unique<quic::FECProcessor>(
        quic::FECAlgorithm::SMPTE_2022_1, fec_params);
    
    // 创建 TS 解析器
    info.ts_parser = std::make_unique<quic::TSParser>();
    
    // 构建 RTMP URL（推流到 ZLM）
    std::string rtmp_url = ::utils::zlm_url_builder::BuildRTMPUrlWithSecret(
        config_, info.target_app, info.target_stream, "QUIC Gateway");
    
    // 创建 FFmpeg 桥接
    info.ffmpeg_bridge = std::make_unique<quic::TSToFFmpegBridge>(
        rtmp_url, process_manager_);
    
    // 创建命名管道
    if (!info.ffmpeg_bridge->CreatePipe()) {
        LOG_ERROR("[QUICGateway] 创建命名管道失败");
        return false;
    }
    
    // 连接数据流：FEC -> TS Parser -> FFmpeg Bridge
    info.fec_processor->SetOutputCallback(
        [ts_parser = info.ts_parser.get()](const uint8_t* data, size_t len) {
            ts_parser->ParsePackets(data, len);
        });
    
    info.ts_parser->SetOutputCallback(
        [bridge = info.ffmpeg_bridge.get()](const uint8_t* data, size_t len) {
            bridge->WritePacket(data, len);
        });
    
    // 启动 FFmpeg（使用默认参数，后续可以根据流信息优化）
    if (!info.ffmpeg_bridge->StartFFmpeg(0, 0, 0)) {
        LOG_ERROR("[QUICGateway] 启动 FFmpeg 失败");
        return false;
    }
    
    info.pid = info.ffmpeg_bridge->GetFFmpegPID();
    info.status = GatewayStatus::Running;
    
    // TODO: 等待 QUIC 连接建立
    // 目前实现中，连接会在 QUIC 服务器收到数据包时自动建立
    // 实际应该根据 source_url 中的信息主动建立连接或等待连接
    
    LOG_INFO("[QUICGateway] QUIC 服务器模式启动成功: {}/{}", 
            info.target_app, info.target_stream);
    return true;
}

bool QUICGateway::StopQuicServerMode(StreamInfo& info) {
    bool success = true;
    
    // 停止 FFmpeg
    if (info.ffmpeg_bridge) {
        if (!info.ffmpeg_bridge->StopFFmpeg()) {
            success = false;
        }
    }
    
    // 关闭 QUIC 连接
    if (info.quic_connection) {
        info.quic_connection->Close();
        info.quic_connection.reset();
    }
    
    // 清理组件
    info.fec_processor.reset();
    info.ts_parser.reset();
    info.ffmpeg_bridge.reset();
    
    info.status = GatewayStatus::Stopped;
    info.pid = 0;
    
    return success;
}


} // namespace gateway

