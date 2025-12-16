#include "gateway/utils/stream_push_template.hpp"
#include "gateway/utils/stream_start_validator.hpp"
#include "gateway/utils/error_codes.hpp"
#include "streaming/zlmediakit/zlm_client.hpp"
#include "streaming/stream_manager.hpp"
#include "process/process_manager.hpp"
#include "process/ffmpeg_executor.hpp"
#include "utils/logger.hpp"
#include <sstream>
#include <algorithm>
#include <cctype>

namespace gateway {
namespace utils {

StreamPushTemplate::StreamPushTemplate(
    std::shared_ptr<streaming::ZLMClient> zlm_client,
    std::shared_ptr<streaming::StreamManager> stream_manager,
    std::shared_ptr<process::ProcessManager> process_manager)
    : zlm_client_(zlm_client),
      stream_manager_(stream_manager),
      process_manager_(process_manager) {
    
    // 创建流启动验证器
    StreamStartValidationConfig config;
    config.process_stable_wait_ms = 500;
    config.zlm_check_interval_ms = 200;    // 缩短检查间隔到 200ms，更快检测流注册
    config.zlm_check_timeout_ms = 30000;  // 增加到 30 秒，考虑 ZLM 的 wait_track_ready_ms=10000 和 maxStreamWaitMS=15000
    config.max_check_attempts = 150;      // 相应增加检查次数（30秒 / 200ms = 150次）
    
    validator_ = std::make_unique<StreamStartValidator>(
        zlm_client_, process_manager_, config);
}

PushStreamResult StreamPushTemplate::PushStream(
    const SourceDescriptor& source_desc,
    const std::string& target_app,
    const std::string& target_stream,
    const std::string& output_protocol,
    const std::string& gateway_type,
    std::function<std::string()> build_ffmpeg_command_callback) {
    
    PushStreamResult result;
    
    // 通过 StreamManager 统一记录"创建请求已发起"
    if (stream_manager_) {
        streaming::StreamMetadata metadata;
        metadata.app = target_app;
        metadata.stream = target_stream;
        metadata.protocol = SourceDescriptor::SourceTypeToString(source_desc.type);
        metadata.output_protocol = output_protocol;
        metadata.source_url = source_desc.source_url;
        metadata.gateway_type = gateway_type;
        metadata.status = streaming::StreamStatus::Starting;
        
        // 设置设备相关字段（如果是设备类型）
        if (source_desc.type == SourceType::LOCAL_CAMERA) {
            metadata.device_id = source_desc.local_camera.device_id;
            metadata.device_type = "local_camera";
        } else if (source_desc.type == SourceType::DEVICE_ONVIF ||
                   source_desc.type == SourceType::DEVICE_ISAPI ||
                   source_desc.type == SourceType::DEVICE_DAHUA ||
                   source_desc.type == SourceType::DEVICE_PSIA) {
            metadata.device_id = source_desc.device.device_id;
            metadata.device_type = SourceDescriptor::SourceTypeToString(source_desc.type);
        }
        
        stream_manager_->OnStreamCreateRequested(metadata);
    }
    
    // 判断是否应该使用 FFmpeg 转码
    bool use_ffmpeg = ShouldUseFFmpeg(source_desc, output_protocol);
    
    if (use_ffmpeg) {
        // 使用 FFmpeg 转码
        if (!build_ffmpeg_command_callback) {
            result.error_code = ErrorCode::FFMPEG_COMMAND_BUILDER_MISSING;
            result.error_message = ErrorMessageMapper::GetMessage(ErrorCode::FFMPEG_COMMAND_BUILDER_MISSING);
            if (stream_manager_) {
                stream_manager_->OnStreamCreateResult(target_app, target_stream, false,
                                                     result.error_code, result.error_message);
            }
            return result;
        }
        
        result = PushWithFFmpeg(source_desc, target_app, target_stream, 
                               output_protocol, gateway_type, build_ffmpeg_command_callback);
    } else {
        // 使用 ZLM API 直接代理
        result = PushWithZLMAPI(source_desc, target_app, target_stream, 
                                output_protocol, gateway_type);
    }
    
    // 报告创建结果
    if (stream_manager_) {
        stream_manager_->OnStreamCreateResult(target_app, target_stream, result.success,
                                             result.error_code, result.error_message);
    }
    
    return result;
}

bool StreamPushTemplate::StopStream(const std::string& target_app,
                                    const std::string& target_stream,
                                    int pid,
                                    const std::string& gateway_type) {
    bool success = true;
    
    // 停止 FFmpeg 进程（如果存在）
    if (pid > 0) {
        std::string stream_id = target_app + "/" + target_stream;
        success = process::FFmpegExecutor::StopProcess(
            pid, process_manager_, stream_id,
            [](gateway::GatewayStatus /* status */) {});
    }
    
    // 删除 ZLM 流
    if (zlm_client_) {
        zlm_client_->DeleteStream(target_app, target_stream);
    }
    
    // 从 StreamManager 注销流
    if (stream_manager_) {
        stream_manager_->UnregisterStream(target_app, target_stream);
    }
    
    return success;
}

PushStreamResult StreamPushTemplate::PushWithZLMAPI(
    const SourceDescriptor& source_desc,
    const std::string& target_app,
    const std::string& target_stream,
    const std::string& /* output_protocol */,
    const std::string& /* gateway_type */) {
    
    PushStreamResult result;
    
    // 根据源类型调用对应的 ZLM API
    bool api_success = false;
    switch (source_desc.type) {
        case SourceType::RTSP_URL:
            api_success = zlm_client_->AddRTSPStream(target_app, target_stream, source_desc.source_url);
            break;
        case SourceType::RTMP_URL:
            api_success = zlm_client_->AddRTMPStream(target_app, target_stream, source_desc.source_url);
            break;
        case SourceType::HTTP_FLV_URL:
        case SourceType::HLS_URL:
            api_success = zlm_client_->AddStreamProxy(target_app, target_stream, source_desc.source_url);
            break;
        default:
            result.error_code = ErrorCode::UNSUPPORTED_SOURCE_TYPE;
            result.error_message = ErrorMessageMapper::GetMessage(ErrorCode::UNSUPPORTED_SOURCE_TYPE);
            result.status = GatewayStatus::Error;
            return result;
    }
    
    if (!api_success) {
        result.error_code = ErrorCode::ZLM_API_FAILED;
        result.error_message = ErrorMessageMapper::GetMessage(ErrorCode::ZLM_API_FAILED);
        result.status = GatewayStatus::Error;
        return result;
    }
    
    // 验证流是否成功启动
    GatewayStatus status = GatewayStatus::Starting;
    bool validated = validator_->ValidateNative(
        status, target_app, target_stream, source_desc.source_url,
        [this, source_desc, target_app, target_stream](const std::string& app, 
                                                       const std::string& stream,
                                                       const std::string& url) -> bool {
            // 重新调用对应的 API（用于验证）
            switch (source_desc.type) {
                case SourceType::RTSP_URL:
                    return zlm_client_->AddRTSPStream(app, stream, url);
                case SourceType::RTMP_URL:
                    return zlm_client_->AddRTMPStream(app, stream, url);
                case SourceType::HTTP_FLV_URL:
                case SourceType::HLS_URL:
                    return zlm_client_->AddStreamProxy(app, stream, url);
                default:
                    return false;
            }
        },
        true  // require_active
    );
    
    if (validated) {
        result.success = true;
        result.status = GatewayStatus::Running;
    } else {
        result.error_code = "STREAM_VALIDATION_FAILED";
        result.error_message = "Stream created but validation failed (no data or timeout)";
        result.status = GatewayStatus::Error;
        MapError("Stream validation failed", result);
    }
    
    return result;
}

PushStreamResult StreamPushTemplate::PushWithFFmpeg(
    const SourceDescriptor& source_desc,
    const std::string& target_app,
    const std::string& target_stream,
    const std::string& /* output_protocol */,
    const std::string& gateway_type,
    std::function<std::string()> build_command_callback) {
    
    PushStreamResult result;
    
    // 构建 FFmpeg 命令
    std::string command = build_command_callback();
    if (command.empty()) {
        result.error_code = ErrorCode::FFMPEG_COMMAND_EMPTY;
        result.error_message = ErrorMessageMapper::GetMessage(ErrorCode::FFMPEG_COMMAND_EMPTY);
        result.status = GatewayStatus::Error;
        return result;
    }
    
    // 启动 FFmpeg 进程
    std::string stream_id = target_app + "/" + target_stream;
    pid_t pid = process::FFmpegExecutor::StartProcess(
        command, process_manager_, stream_id,
        gateway_type + "-" + target_stream,
        source_desc.source_url,
        target_app,
        target_stream
    );
    
    if (pid <= 0) {
        result.error_code = ErrorCode::FFMPEG_START_FAILED;
        result.error_message = ErrorMessageMapper::GetMessage(ErrorCode::FFMPEG_START_FAILED);
        result.status = GatewayStatus::Error;
        return result;
    }
    
    result.pid = pid;
    
    // 验证流是否成功启动
    GatewayStatus status = GatewayStatus::Starting;
    std::string process_id = stream_id;
    
    bool validated = false;
    if (process_manager_) {
        validated = validator_->ValidateWithProcessManager(
            result.pid, status, process_id, target_app, target_stream,
            []() { return true; },  // 进程已启动，不需要再次启动
            nullptr  // 错误日志回调（可选）
        );
    } else {
        validated = validator_->ValidateSimple(
            result.pid, status, target_app, target_stream,
            []() { return 0; },  // 进程已启动，不需要再次启动
            nullptr  // 错误日志回调（可选）
        );
    }
    
    if (validated) {
        result.success = true;
        result.status = GatewayStatus::Running;
    } else {
        result.error_code = ErrorCode::STREAM_VALIDATION_FAILED;
        result.error_message = ErrorMessageMapper::GetMessage(ErrorCode::STREAM_VALIDATION_FAILED);
        result.status = GatewayStatus::Error;
    }
    
    return result;
}

bool StreamPushTemplate::ShouldUseFFmpeg(const SourceDescriptor& source_desc,
                                         const std::string& output_protocol) const {
    // 本地摄像头、本地文件、屏幕捕获等必须使用 FFmpeg
    if (source_desc.type == SourceType::LOCAL_CAMERA ||
        source_desc.type == SourceType::LOCAL_FILE ||
        source_desc.type == SourceType::SCREEN_CAPTURE) {
        return true;
    }
    
    // 设备发现协议通常需要先获取 RTSP URL，然后使用 RTSP Gateway 的逻辑
    // 这里暂时返回 true，后续可以根据实际情况优化
    if (source_desc.type == SourceType::DEVICE_ONVIF ||
        source_desc.type == SourceType::DEVICE_ISAPI ||
        source_desc.type == SourceType::DEVICE_DAHUA ||
        source_desc.type == SourceType::DEVICE_PSIA) {
        return true;  // 设备协议通常需要转码
    }
    
    // 对于 URL 类型，根据 output_protocol 和源流特性决定
    // WebRTC 通常需要转码（确保编码参数符合要求）
    if (output_protocol == "webrtc") {
        return true;
    }
    
    // 其他情况（HTTP-FLV、HLS）可以尝试直接代理，但需要 Gateway 层判断
    // 这里返回 false，让 Gateway 层决定
    return false;
}

void StreamPushTemplate::MapError(const std::string& error_context,
                                 PushStreamResult& result) const {
    // 根据错误上下文映射到统一错误码
    // 如果 result.error_code 已经设置，则使用已设置的错误码
    // 否则根据 error_context 推断错误码
    
    if (!result.error_code.empty()) {
        // 错误码已设置，只需确保错误消息存在
        if (result.error_message.empty()) {
            result.error_message = ErrorMessageMapper::GetMessage(result.error_code);
        }
        return;
    }
    
    // 根据错误上下文推断错误码（转换为小写以便匹配）
    std::string lower_context = error_context;
    std::transform(lower_context.begin(), lower_context.end(), lower_context.begin(), ::tolower);
    
    // 源流相关错误
    if (lower_context.find("404") != std::string::npos ||
        lower_context.find("not found") != std::string::npos ||
        lower_context.find("无法访问") != std::string::npos ||
        lower_context.find("stream not found") != std::string::npos) {
        result.error_code = ErrorCode::SOURCE_NOT_FOUND;
    }
    // 连接超时
    else if (lower_context.find("timeout") != std::string::npos ||
             lower_context.find("超时") != std::string::npos ||
             lower_context.find("timed out") != std::string::npos) {
        if (lower_context.find("source") != std::string::npos ||
            lower_context.find("源流") != std::string::npos) {
            result.error_code = ErrorCode::SOURCE_TIMEOUT;
        } else if (lower_context.find("stream") != std::string::npos ||
                   lower_context.find("流") != std::string::npos) {
            result.error_code = ErrorCode::STREAM_TIMEOUT;
        } else {
            result.error_code = ErrorCode::TIMEOUT;
        }
    }
    // 认证失败
    else if (lower_context.find("401") != std::string::npos ||
             lower_context.find("unauthorized") != std::string::npos ||
             lower_context.find("认证失败") != std::string::npos ||
             lower_context.find("auth failed") != std::string::npos) {
        result.error_code = ErrorCode::SOURCE_AUTH_FAILED;
    }
    // 禁止访问
    else if (lower_context.find("403") != std::string::npos ||
             lower_context.find("forbidden") != std::string::npos ||
             lower_context.find("禁止") != std::string::npos) {
        result.error_code = ErrorCode::SOURCE_FORBIDDEN;
    }
    // 编解码相关
    else if (lower_context.find("codec") != std::string::npos ||
             lower_context.find("编码") != std::string::npos ||
             lower_context.find("unsupported") != std::string::npos ||
             lower_context.find("不支持") != std::string::npos) {
        if (lower_context.find("detect") != std::string::npos ||
            lower_context.find("检测") != std::string::npos) {
            result.error_code = ErrorCode::CODEC_DETECTION_FAILED;
        } else {
            result.error_code = ErrorCode::CODEC_UNSUPPORTED;
        }
    }
    // 网络相关
    else if (lower_context.find("connection refused") != std::string::npos ||
             lower_context.find("连接被拒绝") != std::string::npos) {
        result.error_code = ErrorCode::CONNECTION_REFUSED;
    }
    else if (lower_context.find("dns") != std::string::npos ||
             lower_context.find("host not found") != std::string::npos ||
             lower_context.find("域名") != std::string::npos) {
        result.error_code = ErrorCode::DNS_RESOLUTION_FAILED;
    }
    else if (lower_context.find("network") != std::string::npos ||
             lower_context.find("网络") != std::string::npos ||
             lower_context.find("connection") != std::string::npos ||
             lower_context.find("连接") != std::string::npos) {
        result.error_code = ErrorCode::NETWORK_ERROR;
    }
    // 进程相关
    else if (lower_context.find("process crash") != std::string::npos ||
             lower_context.find("进程崩溃") != std::string::npos ||
             lower_context.find("exit") != std::string::npos ||
             lower_context.find("退出") != std::string::npos) {
        result.error_code = ErrorCode::PROCESS_CRASH;
    }
    else if (lower_context.find("process start") != std::string::npos ||
             lower_context.find("进程启动") != std::string::npos ||
             lower_context.find("start failed") != std::string::npos) {
        result.error_code = ErrorCode::PROCESS_START_FAILED;
    }
    // FFmpeg 相关
    else if (lower_context.find("ffmpeg command") != std::string::npos ||
             lower_context.find("命令为空") != std::string::npos) {
        result.error_code = ErrorCode::FFMPEG_COMMAND_EMPTY;
    }
    else if (lower_context.find("ffmpeg") != std::string::npos) {
        result.error_code = ErrorCode::FFMPEG_START_FAILED;
    }
    // 流验证相关
    else if (lower_context.find("validation failed") != std::string::npos ||
             lower_context.find("验证失败") != std::string::npos ||
             lower_context.find("stream validation") != std::string::npos) {
        result.error_code = ErrorCode::STREAM_VALIDATION_FAILED;
    }
    else if (lower_context.find("stream not ready") != std::string::npos ||
             lower_context.find("流未就绪") != std::string::npos) {
        result.error_code = ErrorCode::STREAM_NOT_READY;
    }
    // ZLM 相关
    else if (lower_context.find("zlm") != std::string::npos ||
             lower_context.find("zlmediakit") != std::string::npos) {
        if (lower_context.find("not available") != std::string::npos ||
            lower_context.find("不可用") != std::string::npos) {
            result.error_code = ErrorCode::ZLM_NOT_AVAILABLE;
        } else if (lower_context.find("api") != std::string::npos) {
            result.error_code = ErrorCode::ZLM_API_FAILED;
        } else {
            result.error_code = ErrorCode::ZLM_STREAM_NOT_FOUND;
        }
    }
    // 未知错误
    else {
        result.error_code = ErrorCode::UNKNOWN_ERROR;
    }
    
    // 获取错误消息
    result.error_message = ErrorMessageMapper::GetMessage(result.error_code);
}

} // namespace utils
} // namespace gateway

