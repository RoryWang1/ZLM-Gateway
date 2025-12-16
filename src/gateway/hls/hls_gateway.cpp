#include "gateway/hls/hls_gateway.hpp"
#include "gateway/utils/gateway_config_helper.hpp"
#include "gateway/utils/error_codes.hpp"
#include "gateway/utils/error_inference.hpp"
#include "config/config_loader.hpp"
#include "streaming/zlmediakit/zlm_client.hpp"
#include "utils/logger.hpp"
#include "gateway/utils/stream_start_validator.hpp"
#include "process/process_manager.hpp"
#include "process/ffmpeg_executor.hpp"
#include "utils/zlm_url_builder.hpp"
#include "utils/ffmpeg_params.hpp"
#include "utils/stream_status_checker.hpp"
#include "gateway/utils/smart_stream_processor.hpp"
#include "gateway/utils/ffmpeg_process_helper.hpp"
#include "gateway/utils/bitrate_allocation_helper.hpp"
#include "gateway/utils/ffmpeg_command_builder.hpp"
#include <sstream>
#include <thread>
#include <chrono>


#include "gateway/utils/gateway_registry.hpp"

namespace gateway {

namespace {
static gateway::utils::AutoRegister reg("hls", [](const gateway::utils::GatewayContext& ctx) -> std::shared_ptr<gateway::GatewayBase> {
    return std::make_shared<HLSGateway>(ctx.config, ctx.zlm_client, ctx.process_manager, ctx.stream_manager);
});
}

HLSGateway::HLSGateway(std::shared_ptr<config::Config> config,
                       std::shared_ptr<streaming::ZLMClient> zlm_client,
                       std::shared_ptr<process::ProcessManager> process_manager,
                       std::shared_ptr<streaming::StreamManager> stream_manager)
    : config_(config), zlm_client_(zlm_client), process_manager_(process_manager), stream_manager_(stream_manager) {
    // 使用配置辅助工具获取路径
    ffmpeg_path_ = gateway::utils::GatewayConfigHelper::GetFFmpegPath(config_);
    ffprobe_path_ = gateway::utils::GatewayConfigHelper::GetFFprobePath(config_);
    zlm_rtsp_url_ = gateway::utils::GatewayConfigHelper::BuildZLMRTSPURL(config_);
    
    // 初始化通用辅助类
    // 1. FFmpeg 进程管理辅助
    ffmpeg_helper_ = std::make_unique<gateway::utils::FFmpegProcessHelper>(process_manager_);
    
    // 2. 码率分配辅助（HLS Gateway 暂时不需要，但为了统一接口保留）
    bitrate_helper_ = std::make_unique<gateway::utils::BitrateAllocationHelper>(nullptr);
    
    // 3. 智能流处理器
    auto stream_info_detector = std::make_unique<gateway::utils::StreamInfoDetector>(ffprobe_path_);
    
     // 4. 初始化 FFmpeg 命令构建器
    ffmpeg_builder_ = std::make_unique<gateway::utils::FFmpegCommandBuilder>(
        config_, ffmpeg_path_, zlm_rtsp_url_);
    
    gateway::utils::StreamStartValidationConfig validator_config;
    // 从配置中读取流验证参数，如果没有配置则使用默认值
    if (config_) {
        validator_config.process_stable_wait_ms = process_manager_ ? config_->gateway.stream_validation.process_stable_wait_ms : 0;
        validator_config.zlm_check_interval_ms = config_->gateway.stream_validation.zlm_check_interval_ms;
        validator_config.zlm_check_timeout_ms = config_->gateway.stream_validation.zlm_check_timeout_ms;
        validator_config.max_check_attempts = config_->gateway.stream_validation.max_check_attempts;
    } else {
        // 默认值（向后兼容）
        validator_config.process_stable_wait_ms = process_manager_ ? 500 : 0;
        validator_config.zlm_check_interval_ms = 500;
        validator_config.zlm_check_timeout_ms = 10000;
        validator_config.max_check_attempts = 20;
    }

    // 强制增加 HLS 的验证超时时间
    // 原因：由于添加了 -re 参数且 FFmpeg 需要探测 (analyzeduration=3s)，HLS 启动延迟显著增加
    // 必须给予足够的时间让流在 ZLM 中注册，否则会导致 StreamStartValidator 误判超时
    if (validator_config.zlm_check_timeout_ms < 20000) {
        validator_config.zlm_check_timeout_ms = 20000;
        LOG_INFO("[HLS Gateway] Increased validation timeout to 20s for -re throttling and analysis");
    }
    
    std::unique_ptr<gateway::utils::StreamStartValidator> stream_start_validator = nullptr;
    if (zlm_client_) {
        stream_start_validator = std::make_unique<gateway::utils::StreamStartValidator>(
            zlm_client_, process_manager_, validator_config);
    }
    
    smart_processor_ = std::make_unique<gateway::utils::SmartStreamProcessor>(
        zlm_client_, stream_manager_, process_manager_,
        std::move(stream_info_detector),
        std::move(stream_start_validator),
        config_);
    
    LOG_INFO("HLS Gateway 初始化完成（支持直接代理和转码）");
}

HLSGateway::~HLSGateway() {
    // 停止所有流（包括直接代理和转码）
    GatewayBase::StopAllStreamsWithFFmpeg<StreamInfo>(
        streams_, streams_mutex_,
        [this](StreamInfo& info) { 
            if (info.pid > 0) {
                std::string stream_id = info.target_app + "/" + info.target_stream;
                ffmpeg_helper_->StopProcess(info.pid, stream_id,
                    [&info](GatewayStatus status) { info.status = status; });
            } else {
                // 直接代理模式，使用原生停止
                if (zlm_client_) {
                    zlm_client_->DeleteStream(info.target_app, info.target_stream);
                }
            }
        });
}

Result<void> HLSGateway::Start(const std::string& source_url,
                      const std::string& target_app,
                      const std::string& target_stream,
                      const std::string& output_protocol) {
    std::string stream_id = target_app + "/" + target_stream;
    
    // 检查流是否已存在
    auto [exists, is_running] = GatewayBase::CheckStreamExists<StreamInfo>(
        stream_id, streams_, streams_mutex_, target_app, target_stream,
        [this, target_app, target_stream](StreamInfo& info) {
            if (info.pid > 0) {
                std::string stream_id = info.target_app + "/" + info.target_stream;
                ffmpeg_helper_->StopProcess(info.pid, stream_id,
                    [&info](GatewayStatus status) { info.status = status; });
            }
            // 确保 ZLM 中的代理也被清理
            if (zlm_client_) {
                zlm_client_->DeleteStream(target_app, target_stream);
            }
        });
    
    if (exists && is_running) {
        return Result<void>::Success();
    }
    
    std::lock_guard<std::mutex> lock(streams_mutex_);  // CheckStreamExists已经释放锁，这里重新加锁

    // 创建流信息
    StreamInfo info;
    info.source_url = source_url;
    info.target_app = target_app;
    info.target_stream = target_stream;
    info.output_protocol = output_protocol; // 保存输出协议
    info.status = GatewayStatus::Starting;

    std::string resolved_output = output_protocol.empty() ? "hls" : output_protocol;

    // 使用 SmartStreamProcessor 处理流启动
    auto result = smart_processor_->ProcessStream(
        source_url, target_app, target_stream, resolved_output, "hls", "hls_gateway",
        // 直接代理回调
        [this](const std::string& app, const std::string& stream, const std::string& url) {
            return zlm_client_ && zlm_client_->AddStreamProxy(app, stream, url);
        },
        // 获取流信息回调
        [this](const std::string& app, const std::string& stream, const std::string& schema) {
            return zlm_client_ ? zlm_client_->GetStreamInfo(app, stream, schema.empty() ? "hls" : schema) : streaming::StreamInfo();
        },
        // 启动 FFmpeg 回调
        [this, &info, stream_id](const gateway::utils::StreamInfoResult& stream_info_result) {
            if (!process_manager_) {
                LOG_ERROR("[HLS Gateway] ProcessManager not available, cannot use FFmpeg transcoding");
                return 0;
            }
            
            gateway::utils::FFmpegCommandOptions options;
            options.input_url = info.source_url;
            options.input_format = ""; // Auto-detect for HLS
            options.output_protocol = info.output_protocol;
            options.target_app = info.target_app;
            options.target_stream = info.target_stream;
            options.target_bitrate_kbps = 0; // HLS uses 0 for default/smart bitrate if not specified
            options.gateway_name = "HLS Gateway";
            options.stream_info = stream_info_result;

            std::string command = ffmpeg_builder_->BuildCommand(options);
            int pid = 0;
            GatewayStatus status = GatewayStatus::Stopped;
            
            std::string log_file = "/tmp/ffmpeg_hls_" + info.target_app + "_" + info.target_stream + ".log";
            if (ffmpeg_helper_->StartProcess(command, stream_id, log_file, pid, status)) {
                return pid;
            }
            return 0;
        },
        // 读取错误日志回调
        [this, target_app, target_stream]() {
            std::string stream_id = target_app + "/" + target_stream;
            return ffmpeg_helper_->ReadErrorLog(stream_id);
        },
        "hls"  // stream_schema
    );
    
    // 更新流信息
    info.source_audio_codec = result.source_audio_codec;
    info.source_video_codec = result.source_video_codec;
    info.source_width = result.source_width;
    info.source_height = result.source_height;
    info.video_only_transcode = result.video_only_transcode;
    info.pid = result.pid;
    info.status = result.status;
    
    if (result.success) {
        streams_[stream_id] = info;
        return Result<void>::Success();
    } else {
        // 错误已在 SmartStreamProcessor 中处理
        info.status = GatewayStatus::Error;
        streams_[stream_id] = info;
        return Result<void>::Failure(gateway::InternalServerException(result.error_message));
    }
}

Result<void> HLSGateway::Stop(const std::string& target_app,
                      const std::string& target_stream) {
    std::string stream_id = target_app + "/" + target_stream;
    return GatewayBase::StopWithFFmpeg<StreamInfo>(
        stream_id, target_app, target_stream,
        streams_, streams_mutex_,
        [this](StreamInfo& info) { 
            if (info.pid > 0) {
                std::string stream_id = info.target_app + "/" + info.target_stream;
                return ffmpeg_helper_->StopProcess(info.pid, stream_id,
                    [&info](GatewayStatus status) { info.status = status; });
            }
            return true;
        },
        [this](const std::string& app, const std::string& stream) {
            if (zlm_client_) {
                zlm_client_->DeleteStream(app, stream);
            }
        },
        "HLS");
}

bool HLSGateway::IsRunning(const std::string& target_app,
                      const std::string& target_stream) {
    
    std::lock_guard<std::mutex> lock(streams_mutex_);
    
    auto it = streams_.find(target_app + "/" + target_stream);
    if (it == streams_.end()) {
        return false;
    }

    StreamInfo& info = it->second;
    
    // 使用工具类检查流状态
    gateway::GatewayStatus new_status;
    int new_pid;
    bool is_running = ::utils::StreamStatusChecker::CheckIsRunning(
        info.pid,
        info.status,
        zlm_client_,
        target_app,
        target_stream,
        new_status,
        new_pid
    );
    
    info.status = new_status;
    info.pid = new_pid;
    
    return is_running;
}

GatewayStatus HLSGateway::GetStatus(const std::string& target_app,
                                   const std::string& target_stream) {
    std::string stream_id = target_app + "/" + target_stream;
    
    std::lock_guard<std::mutex> lock(streams_mutex_);
    
    auto it = streams_.find(stream_id);
    if (it == streams_.end()) {
        return GatewayStatus::Stopped;
    }

    StreamInfo& info = it->second;
    
    // 使用工具类获取流状态
    int new_pid;
    gateway::GatewayStatus new_status = ::utils::StreamStatusChecker::GetStatus(
        info.pid,
        info.status,
        zlm_client_,
        target_app,
        target_stream,
        new_pid
    );
    
    info.status = new_status;
    info.pid = new_pid;
    
    return new_status;
}




} // namespace gateway
