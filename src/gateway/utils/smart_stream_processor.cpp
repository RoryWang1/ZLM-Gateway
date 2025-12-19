#include "gateway/utils/smart_stream_processor.hpp"
#include "gateway/utils/error_codes.hpp"
#include "gateway/utils/error_inference.hpp"
#include "gateway/base/gateway_base.hpp"
#include "config/config_loader.hpp"
#include "config/constants.hpp"
#include "utils/logger.hpp"
#include <thread>
#include <chrono>

using namespace config::constants;

namespace gateway {
namespace utils {

SmartStreamProcessor::SmartStreamProcessor(
    std::shared_ptr<streaming::ZLMClient> zlm_client,
    std::shared_ptr<streaming::StreamManager> stream_manager,
    std::shared_ptr<process::ProcessManager> process_manager,
    std::unique_ptr<StreamInfoDetector> stream_info_detector,
    std::unique_ptr<StreamStartValidator> stream_start_validator,
    std::shared_ptr<config::Config> config)
    : zlm_client_(zlm_client),
      stream_manager_(stream_manager),
      process_manager_(process_manager),
      stream_info_detector_(std::move(stream_info_detector)),
      stream_start_validator_(std::move(stream_start_validator)),
      config_(config) {
    
    // 如果没有提供检测器，创建一个默认的
    if (!stream_info_detector_) {
        stream_info_detector_ = std::make_unique<StreamInfoDetector>();
    }
    
    // 从配置中读取直接代理参数，如果没有配置则使用默认值
    if (config_) {
        max_retries_ = config_->gateway.direct_proxy.max_retries;
        retry_interval_ms_ = config_->gateway.direct_proxy.retry_interval_ms;
    } else {
        // 默认值（向后兼容）
        max_retries_ = 5;
        retry_interval_ms_ = 2000;
    }
    
    // 如果没有提供验证器，创建一个默认的（需要 config）
    // 注意：这里暂时不创建，因为需要 config，让调用者传入
}

StreamProcessResult SmartStreamProcessor::ProcessStream(
    const std::string& source_url,
    const std::string& target_app,
    const std::string& target_stream,
    const std::string& output_protocol,
    const std::string& protocol,
    const std::string& gateway_type,
    DirectProxyCallback direct_proxy_callback,
    GetStreamInfoCallback get_stream_info_callback,
    StartFFmpegCallback start_ffmpeg_callback,
    ReadFFmpegErrorCallback read_ffmpeg_error_callback,
    const std::string& stream_schema,
    int detect_timeout,
    const std::string& detect_username,
    const std::string& detect_password) {
    
    StreamProcessResult result;
    
    // 1. 通过 StreamManager 统一记录"创建请求已发起"，状态设为 Starting
    if (stream_manager_) {
        streaming::StreamMetadata metadata;
        metadata.app = target_app;
        metadata.stream = target_stream;
        metadata.protocol = protocol;
        metadata.output_protocol = output_protocol;
        metadata.source_url = source_url;
        metadata.gateway_type = gateway_type;
        metadata.status = streaming::StreamStatus::Starting;
        
        // 对于 local-camera 协议，从 source_url 提取 device_id
        if (protocol == "local-camera" && !source_url.empty()) {
            // 支持两种格式：
            // 1. local-camera://device_id:{device_id} (新格式)
            // 2. local-camera://{index} (旧格式，兼容性)
            size_t pos = source_url.find("local-camera://");
            if (pos != std::string::npos) {
                std::string remaining = source_url.substr(pos + 15); // 跳过 "local-camera://"
                
                // 检查是否是 device_id 格式
                if (remaining.find("device_id:") == 0) {
                    // 新格式: local-camera://device_id:{device_id}
                    std::string device_id_part = remaining.substr(10); // 跳过 "device_id:"
                    size_t end_pos = device_id_part.find_first_of("?/");
                    if (end_pos != std::string::npos) {
                        metadata.device_id = device_id_part.substr(0, end_pos);
                    } else {
                        metadata.device_id = device_id_part;
                    }
                    metadata.device_type = "local_camera";
                }
            }
        }
        
        stream_manager_->OnStreamCreateRequested(metadata);
    }
    
    // 2. 检测流信息
    result.stream_info = DetectStreamInfo(source_url, detect_timeout, detect_username, detect_password);
    
    if (result.stream_info.valid) {
        result.source_audio_codec = result.stream_info.audio_codec;
        result.source_video_codec = result.stream_info.video_codec;
        result.source_width = result.stream_info.video_width;
        result.source_height = result.stream_info.video_height;
        
        LOG_INFO("[SmartStreamProcessor] Detected stream info - Audio: {} ({}Hz, {}ch, {}kbps), Video: {} ({}x{}@{}fps, {}kbps), Total: {}kbps", 
                 result.stream_info.audio_codec.empty() ? "(none)" : result.stream_info.audio_codec,
                 result.stream_info.audio_sample_rate > 0 ? result.stream_info.audio_sample_rate : 0,
                 result.stream_info.audio_channels > 0 ? result.stream_info.audio_channels : 0,
                 result.stream_info.audio_bitrate > 0 ? result.stream_info.audio_bitrate / 1000 : 0,
                 result.stream_info.video_codec.empty() ? "(none)" : result.stream_info.video_codec,
                 result.stream_info.video_width, result.stream_info.video_height,
                 result.stream_info.video_fps > 0.0 ? result.stream_info.video_fps : 0.0,
                 result.stream_info.video_bitrate > 0 ? result.stream_info.video_bitrate / 1000 : 0,
                 result.stream_info.total_bitrate > 0 ? result.stream_info.total_bitrate / 1000 : 0);
    } else {
        LOG_WARN("[SmartStreamProcessor] Stream info detection failed, will try direct proxy first");
    }
    
    // 3. 智能转码决策
    bool use_ffmpeg = false;
    bool video_only_transcode = false;

    MakeTranscodingDecision(result.stream_info, protocol, output_protocol, use_ffmpeg, video_only_transcode, result.transcoding_reason);
    result.use_ffmpeg = use_ffmpeg;
    result.use_ffmpeg = use_ffmpeg;

    result.video_only_transcode = video_only_transcode;
    
    LOG_INFO("[SmartStreamProcessor] 🎯 After MakeTranscodingDecision: use_ffmpeg={}, video_only_transcode={}", 
             use_ffmpeg, video_only_transcode);
    
    // Calculate processing type for metadata
    std::string processing_type = "0"; // Direct Proxy
    if (use_ffmpeg) {
        if (output_protocol == "webrtc" || protocol == "hls") {
            processing_type = "2"; // Transcode (WebRTC strict or HLS throttled)
        } else if (video_only_transcode) {
            processing_type = "1"; // Copy (Video Copy, Audio Transcode)
        } else {
            processing_type = "2"; // Transcode (Full)
        }
    }
    
    // 4. 尝试直接代理（如果不需要 FFmpeg）
    if (!use_ffmpeg) {
        std::string proxy_error_code;
        std::string proxy_error_message;
        DirectProxyResult proxy_result = TryDirectProxy(
            target_app, target_stream, source_url,
            direct_proxy_callback, get_stream_info_callback,
            stream_schema.empty() ? protocol : stream_schema,
            proxy_error_code, proxy_error_message);
        
        if (proxy_result == DirectProxyResult::Success) {
            // 直接代理成功
            result.success = true;
            result.pid = 0;
            result.status = GatewayStatus::Running;
            result.use_ffmpeg = false;
            
            // 注册流到 StreamManager
            RegisterStream(target_app, target_stream, protocol, output_protocol,
                          source_url, gateway_type, result.status, 0, processing_type, result.transcoding_reason);
            
            // 报告成功
            ReportStreamResult(target_app, target_stream, true);
            
            return result;
        } else if (proxy_result == DirectProxyResult::PermanentError) {
            // 永久错误（源流不存在、认证失败等），直接回退到 FFmpeg 转码
            LOG_WARN("[SmartStreamProcessor] Direct proxy failed with permanent error ({}: {}), falling back to FFmpeg transcoding", 
                    proxy_error_code, proxy_error_message);
            use_ffmpeg = true;
            result.use_ffmpeg = true;
        } else {
            // 临时错误或超时
            // 对于 HTTP-FLV 等长连接流，即使 addStreamProxy API 返回超时，流可能已经在 ZLM 中创建
            // 检查流是否真的存在，如果存在则继续等待数据传输
            if (protocol == "http-flv" || protocol == "hls") {
                LOG_WARN("[SmartStreamProcessor] Direct proxy API returned timeout for {} stream ({}: {}), but checking if stream was actually created", 
                        protocol, proxy_error_code, proxy_error_message);
                
                // 等待一段时间，然后检查流是否真的创建了
                LOG_INFO("[SmartStreamProcessor] Waiting {} seconds before checking if stream was created...", time::SMART_STREAM_FALLBACK_WAIT_MS / 1000);
                std::this_thread::sleep_for(std::chrono::milliseconds(time::SMART_STREAM_FALLBACK_WAIT_MS));
                
                auto check_stream_info = get_stream_info_callback(target_app, target_stream, stream_schema.empty() ? protocol : stream_schema);
                LOG_INFO("[SmartStreamProcessor] Checked stream info: app={}, stream={}, alive={}, speed={} bytes/s, total={} bytes", 
                        check_stream_info.app, check_stream_info.stream, check_stream_info.alive, 
                        check_stream_info.bytes_speed, check_stream_info.total_bytes);
                
                if (check_stream_info.app == target_app && check_stream_info.stream == target_stream) {
                    // 流已创建！继续等待数据传输（给更多时间）
                    LOG_INFO("[SmartStreamProcessor] ✅ Stream {}/{} was created despite API timeout, waiting for data transfer...", 
                            target_app, target_stream);
                    
                    // 再等待一段时间，检查是否有数据传输（增加到 30 次，每次 2 秒，总共 60 秒）
                    for (int wait_retry = 0; wait_retry < 30; ++wait_retry) {
                        std::this_thread::sleep_for(std::chrono::milliseconds(time::SMART_STREAM_RETRY_INTERVAL_MS));
                        auto wait_stream_info = get_stream_info_callback(target_app, target_stream, stream_schema.empty() ? protocol : stream_schema);
                        
                        if (wait_retry % 5 == 0) {
                            LOG_INFO("[SmartStreamProcessor] Waiting for data transfer: retry {}/30, alive={}, speed={} bytes/s, total={} bytes", 
                                    wait_retry + 1, wait_stream_info.alive, wait_stream_info.bytes_speed, wait_stream_info.total_bytes);
                        }
                        
                        if (streaming::ZLMClient::IsStreamActive(wait_stream_info)) {
                            // 流已活跃，直接代理成功！
                            LOG_INFO("[SmartStreamProcessor] ✅✅✅ Direct proxy successful after API timeout: {}/{} (alive={}, speed={} bytes/s, total={} bytes, waited {} seconds)", 
                                    target_app, target_stream, wait_stream_info.alive, wait_stream_info.bytes_speed, 
                                    wait_stream_info.total_bytes, (wait_retry + 1) * 2);
                            result.success = true;
                            result.pid = 0;
                            result.status = GatewayStatus::Running;
                            result.use_ffmpeg = false;
                            
                            // 注册流到 StreamManager
                            RegisterStream(target_app, target_stream, protocol, output_protocol,
                                          source_url, gateway_type, result.status, 0, processing_type, result.transcoding_reason);
                            
                            // 报告成功
                            ReportStreamResult(target_app, target_stream, true);
                            
                            return result;
                        }
                    }
                    
                    // 等待超时，流已创建但无数据传输，回退到 FFmpeg
                    LOG_WARN("[SmartStreamProcessor] ❌ Stream {}/{} was created but no data transfer after 60 seconds, falling back to FFmpeg", 
                            target_app, target_stream);
                    if (zlm_client_) {
                        zlm_client_->DeleteStream(target_app, target_stream);
                    }
                } else {
                    LOG_WARN("[SmartStreamProcessor] Stream {}/{} was not created in ZLM after API timeout", 
                            target_app, target_stream);
                }
            }
            
            // 回退到 FFmpeg 转码
            LOG_WARN("[SmartStreamProcessor] Direct proxy failed with transient error ({}: {}), falling back to FFmpeg transcoding", 
                    proxy_error_code, proxy_error_message);
            use_ffmpeg = true;
            result.use_ffmpeg = true;
        }
    }
    
    // 5. FFmpeg 转码模式
    if (use_ffmpeg) {
        bool ffmpeg_success = StartFFmpegTranscode(
            target_app, target_stream,
            result.stream_info,
            start_ffmpeg_callback, read_ffmpeg_error_callback,
            result.pid, result.status);
        
        if (ffmpeg_success) {
            result.success = true;
            
            // 注册流到 StreamManager
            RegisterStream(target_app, target_stream, protocol, output_protocol,
                          source_url, gateway_type, result.status, result.pid, processing_type, result.transcoding_reason);
            
            // 报告成功
            ReportStreamResult(target_app, target_stream, true);
        } else {
            result.success = false;
            result.status = GatewayStatus::Error;
            
            // 推断错误码
            std::string error_log = read_ffmpeg_error_callback();
            auto [error_code, error_message] = InferErrorFromLog(error_log);
            result.error_code = error_code;
            result.error_message = error_message;
            
            // 注册错误状态到 StreamManager
            RegisterStream(target_app, target_stream, protocol, output_protocol,
                          source_url, gateway_type, result.status, result.pid, processing_type, result.transcoding_reason);
            
            // 报告失败
            ReportStreamResult(target_app, target_stream, false, error_code, error_message);
        }
    }
    
    return result;
}

void SmartStreamProcessor::RegisterStream(const std::string& target_app,
                                         const std::string& target_stream,
                                         const std::string& protocol,
                                         const std::string& output_protocol,
                                         const std::string& source_url,
                                         const std::string& gateway_type,
                                         GatewayStatus status,
                                         int pid,
                                         const std::string& processing_type,
                                         const std::string& transcoding_reason) {
    if (!stream_manager_) {
        return;
    }
    
    streaming::StreamMetadata metadata;
    metadata.app = target_app;
    metadata.stream = target_stream;
    metadata.protocol = protocol;
    metadata.output_protocol = output_protocol;
    metadata.source_url = source_url;
    metadata.gateway_type = gateway_type;
    metadata.status = (status == GatewayStatus::Running) ? 
                    streaming::StreamStatus::Running : streaming::StreamStatus::Starting;
    metadata.pid = pid;
    metadata.processing_type = processing_type;
    metadata.transcoding_reason = transcoding_reason;
    
    // 对于 local-camera 协议，从 source_url 提取 device_id
    if (protocol == "local-camera" && !source_url.empty()) {
        // 支持两种格式：
        // 1. local-camera://device_id:{device_id} (新格式)
        // 2. local-camera://{index} (旧格式，兼容性)
        size_t pos = source_url.find("local-camera://");
        if (pos != std::string::npos) {
            std::string remaining = source_url.substr(pos + 15); // 跳过 "local-camera://"
            
            // 检查是否是 device_id 格式
            if (remaining.find("device_id:") == 0) {
                // 新格式: local-camera://device_id:{device_id}
                std::string device_id_part = remaining.substr(10); // 跳过 "device_id:"
                size_t end_pos = device_id_part.find_first_of("?/");
                if (end_pos != std::string::npos) {
                    metadata.device_id = device_id_part.substr(0, end_pos);
                } else {
                    metadata.device_id = device_id_part;
                }
                metadata.device_type = "local_camera";
            }
        }
    }
    
    stream_manager_->RegisterStream(metadata);
}

void SmartStreamProcessor::ReportStreamResult(const std::string& target_app,
                                              const std::string& target_stream,
                                              bool success,
                                              const std::string& error_code,
                                              const std::string& error_message) {
    if (stream_manager_) {
        stream_manager_->OnStreamCreateResult(target_app, target_stream, success, error_code, error_message);
    }
}

StreamInfoResult SmartStreamProcessor::DetectStreamInfo(const std::string& source_url,
                                                        int timeout_seconds,
                                                        const std::string& username,
                                                        const std::string& password) {
    if (!stream_info_detector_) {
        return StreamInfoResult();
    }
    
    if (!username.empty() && !password.empty()) {
        return stream_info_detector_->Detect(source_url, username, password, timeout_seconds);
    } else {
        return stream_info_detector_->Detect(source_url, timeout_seconds);
    }
}

void SmartStreamProcessor::MakeTranscodingDecision(const StreamInfoResult& stream_info_result,
                                                   const std::string& protocol,
                                                   const std::string& output_protocol,
                                                   bool& use_ffmpeg,
                                                   bool& video_only_transcode,
                                                   std::string& transcoding_reason) {
    use_ffmpeg = false;
    video_only_transcode = false;
    transcoding_reason = "";
    
    // 1. 特殊协议处理：HLS
    // HLS 源可能是 VOD（点播），ZLM 直接代理会全速读取导致卡顿
    // 必须使用 FFmpeg + -re 参数来限制读取速度为实时
    if (protocol == "hls") {
        LOG_INFO("[SmartStreamProcessor] HLS protocol detected, forcing FFmpeg to enable -re throttling for VOD sources");
        use_ffmpeg = true;
        transcoding_reason = "HLS Input (VOD Throttling)";
        return;
    }

    // 2. 特殊输出协议处理：WebRTC
    // 使用统一的 WebRTC 兼容性策略
    if (output_protocol == "webrtc") {
        std::string compatibility_reason;
        
        // 获取配置（如果有）
        config::Config::GatewayConfig::WebRTCCompatibilityConfig webrtc_config;
        if (config_) {
            webrtc_config = config_->gateway.webrtc_compat;
        } else {
             // 默认值（如果没有配置对象），虽然 config_loader 已有默认值，但防守编程
             webrtc_config.allowed_profiles = {"Baseline", "Constrained Baseline"};
             webrtc_config.allowed_pixel_formats = {"yuv420p"};
             webrtc_config.allowed_audio_codecs = {"aac", "opus", "pcma", "pcmu", "g711"};
        }

        bool compatible = StreamInfoDetector::IsWebRTCCompatible(stream_info_result, webrtc_config, compatibility_reason);
        
        if (!compatible) {
             LOG_INFO("[SmartStreamProcessor] WebRTC Incompatible: {}, falling back to FFmpeg (Reason: {})", 
                      compatibility_reason, compatibility_reason);
             use_ffmpeg = true;
             transcoding_reason = compatibility_reason;
             // 注意：这里暂时不区分 Video Only Transcode。如果 IsWebRTCCompatible 返回 false，通常意味着视频或音频至少有一个不行。
             // 更精细的 StreamInfoDetector::CanUseVideoCopy 可以在这里使用，但 FFprobeDetector 已经能生成优化参数。
             // 只要 use_ffmpeg = true，FFmpegCommandBuilder 会根据输入输出自动决定 Copy 还是 Transcode。
             // 后续可以优化 video_only_transcode 标志位以更精确控制 metadata
             if (StreamInfoDetector::IsVideoCodecCompatible(stream_info_result.video_codec) &&
                 !StreamInfoDetector::IsAudioCodecCompatible(stream_info_result.audio_codec)) {
                 video_only_transcode = true; // 仅音频转码
             }
             return;
        } else {
             LOG_INFO("[SmartStreamProcessor] WebRTC Compatible (Deep Check Passed), allowing Direct Proxy");
        }
    }
    
    // 3. 智能兼容性检测 (Direct Proxy First)
    // 即使不是 WebRTC，或者 WebRTC 检查通过，最后再做一次通用检查
    if (stream_info_result.valid) {
        // 使用检测到的完整流信息进行智能决策
        bool video_compatible = StreamInfoDetector::IsVideoCodecCompatible(stream_info_result.video_codec);
        bool audio_compatible = StreamInfoDetector::IsAudioCodecCompatible(stream_info_result.audio_codec);
        
        if (video_compatible && audio_compatible) {
            // 视频和音频都兼容，优先使用直接代理
            // 这包括 RTSP, RTMP, HTTP-FLV 等常见协议
            use_ffmpeg = false;
            transcoding_reason = ""; // Direct Proxy 不需要原因
            LOG_INFO("[SmartStreamProcessor] Video({}) and audio({}) are both compatible, using direct proxy", 
                    stream_info_result.video_codec, stream_info_result.audio_codec);
        } else if (video_compatible && !audio_compatible) {
            // 视频兼容但音频不兼容（如 H.264 + MP3），只转码音频
            use_ffmpeg = true;
            video_only_transcode = true;
            transcoding_reason = "Audio Incompatible (" + stream_info_result.audio_codec + ")";
            LOG_INFO("[SmartStreamProcessor] Video({}) is compatible but audio({}) is not, transcoding audio only", 
                    stream_info_result.video_codec, stream_info_result.audio_codec);
        } else {
            // 视频不兼容（如 H.265），需要全转码
            use_ffmpeg = true;
            video_only_transcode = false;
            transcoding_reason = "Video Incompatible (" + stream_info_result.video_codec + ")";
            LOG_INFO("[SmartStreamProcessor] Video({}) is not compatible, transcoding video and audio", 
                    stream_info_result.video_codec.empty() ? "(unknown)" : stream_info_result.video_codec);
        }
    } else {
        // 4. 检测失败兜底
        // 检测失败时，优先尝试直接代理（包括 HTTP-FLV 和 HLS）
        // 即使流信息检测失败，也尝试直接代理，因为 ZLM 可能能够处理
        // 如果直接代理失败（如 ZLM_API_FAILED 或 Timeout），ProcessStream 会处理回退到 FFmpeg
        use_ffmpeg = false;
        transcoding_reason = "Detection Failed, Trying Direct Proxy";
        LOG_WARN("[SmartStreamProcessor] Stream info detection failed for {} stream, will try direct proxy first. If it fails, will fallback to FFmpeg transcoding", protocol);
    }
}

SmartStreamProcessor::DirectProxyResult SmartStreamProcessor::TryDirectProxy(
    const std::string& target_app,
    const std::string& target_stream,
    const std::string& source_url,
    DirectProxyCallback direct_proxy_callback,
    GetStreamInfoCallback get_stream_info_callback,
    const std::string& stream_schema,
    std::string& error_code,
    std::string& error_message) {
    
    // 调用直接代理回调
    LOG_INFO("[SmartStreamProcessor] Calling direct proxy callback for {}/{} -> {}", target_app, target_stream, source_url);
    bool callback_result = direct_proxy_callback(target_app, target_stream, source_url);
    LOG_INFO("[SmartStreamProcessor] Direct proxy callback returned: {}", callback_result ? "true (success)" : "false (failed)");
    
    if (!callback_result) {
        // 直接代理回调失败，尝试推断错误类型
        // 注意：这里无法直接获取 ZLM API 的错误消息，因为回调只返回 bool
        // 但我们可以根据 URL 类型和常见错误模式进行推断
        error_code = ErrorCode::ZLM_API_FAILED;
        error_message = ErrorMessageMapper::GetMessage(ErrorCode::ZLM_API_FAILED);
        
        // 尝试从 URL 推断错误类型（如果可能）
        std::string lower_url = source_url;
        std::transform(lower_url.begin(), lower_url.end(), lower_url.begin(), ::tolower);
        
        // 检查是否是认证相关错误（RTSP URL 通常包含认证信息）
        if (lower_url.find("rtsp://") == 0 && lower_url.find("@") != std::string::npos) {
            // RTSP URL 包含认证信息，可能是认证失败
            error_code = ErrorCode::SOURCE_AUTH_FAILED;
            error_message = ErrorMessageMapper::GetMessage(ErrorCode::SOURCE_AUTH_FAILED);
        }
        
        // 对于 HTTP-FLV 等长连接流，即使回调返回 false，也可能是超时（流可能已创建）
        // 这里返回 Timeout，让上层继续检查
        if (lower_url.find("http://") == 0 || lower_url.find("https://") == 0) {
            LOG_WARN("[SmartStreamProcessor] Direct proxy callback failed for HTTP/HTTPS stream {}/{}: {} ({}), but may be timeout - will check if stream was created", 
                    target_app, target_stream, error_message, error_code);
            return DirectProxyResult::Timeout;  // 返回 Timeout，让上层检查流是否已创建
        }
        
        LOG_WARN("[SmartStreamProcessor] Direct proxy callback failed for {}/{}: {} ({})", 
                target_app, target_stream, error_message, error_code);
        return DirectProxyResult::PermanentError;  // API 调用失败通常是永久错误
    }
    
    // 等待并检查流是否真的活跃
    // 使用改进的重试机制：从配置读取重试参数
    for (int retry = 0; retry < max_retries_; ++retry) {
        std::this_thread::sleep_for(std::chrono::milliseconds(retry_interval_ms_));
        auto stream_info = get_stream_info_callback(target_app, target_stream, stream_schema);
        
        // 使用统一的流活跃性判断函数
        if (streaming::ZLMClient::IsStreamActive(stream_info)) {
            // 流已成功建立且有数据传输
            LOG_INFO("[SmartStreamProcessor] Direct proxy successful, stream is active: {}/{} (alive={}, speed={} bytes/s, total={} bytes, waited {}ms)", 
                    target_app, target_stream, stream_info.alive, stream_info.bytes_speed, 
                    stream_info.total_bytes, (retry + 1) * retry_interval_ms_);
            error_code.clear();
            error_message.clear();
            return DirectProxyResult::Success;
        }
        
        // 记录重试日志（仅在最后一次重试时记录警告）
        if (retry < max_retries_ - 1) {
            LOG_DEBUG("[SmartStreamProcessor] Direct proxy stream not yet active, waiting for retry {}/{}: {}/{} (alive={}, speed={} bytes/s)", 
                    retry + 1, max_retries_, target_app, target_stream, 
                    stream_info.alive, stream_info.bytes_speed);
        }
    }
    
    // 所有重试都失败，流未活跃
    auto final_stream_info = get_stream_info_callback(target_app, target_stream, stream_schema);
    
    // 判断错误类型：如果流已注册但无数据传输，可能是源流问题
    // 但对于 HTTP-FLV 等长连接流，可能需要更长时间才能开始传输数据
    if (final_stream_info.app == target_app && final_stream_info.stream == target_stream) {
        // 流已注册但无数据传输
        // 对于 HTTP-FLV 长连接流，即使已注册，也可能需要等待一段时间才能开始传输数据
        // 如果流已注册（说明 ZLM 已连接到源流），即使暂时没有数据，也不应该立即判定为失败
        // 这里改为超时错误，允许回退到 FFmpeg 转码（FFmpeg 可以更好地处理长连接流）
        error_code = ErrorCode::STREAM_TIMEOUT;
        error_message = ErrorMessageMapper::GetMessage(ErrorCode::STREAM_TIMEOUT);
        LOG_WARN("[SmartStreamProcessor] Direct proxy created but stream not active (no data transfer in {} seconds), may need more time for long-lived streams: {}/{} (alive={}, speed={} bytes/s)", 
                max_retries_ * retry_interval_ms_ / 1000, target_app, target_stream, 
                final_stream_info.alive, final_stream_info.bytes_speed);
        // 对于长连接流，即使已注册但无数据，也视为超时，允许回退到 FFmpeg 转码
        // 清理直接代理的流
        if (zlm_client_) {
            zlm_client_->DeleteStream(target_app, target_stream);
        }
        return DirectProxyResult::Timeout;  // 改为 Timeout，允许回退到 FFmpeg
    } else {
        // 流未注册，可能是超时或临时网络问题
        error_code = ErrorCode::STREAM_TIMEOUT;
        error_message = ErrorMessageMapper::GetMessage(ErrorCode::STREAM_TIMEOUT);
        LOG_WARN("[SmartStreamProcessor] Direct proxy stream not registered after {} seconds, timeout: {}/{}", 
                max_retries_ * retry_interval_ms_ / 1000, target_app, target_stream);
        // 超时可能是临时问题，但当前实现中仍视为需要回退
        return DirectProxyResult::Timeout;
    }
}

bool SmartStreamProcessor::StartFFmpegTranscode(const std::string& target_app,
                                                const std::string& target_stream,
                                                const StreamInfoResult& stream_info_result,
                                                StartFFmpegCallback start_ffmpeg_callback,
                                                ReadFFmpegErrorCallback read_ffmpeg_error_callback,
                                                int& pid,
                                                GatewayStatus& status) {
    LOG_INFO("[SmartStreamProcessor] ▶️  StartFFmpegTranscode called for {}/{}", target_app, target_stream);
    
    if (!stream_start_validator_) {
        // 如果没有验证器，直接调用回调
        LOG_INFO("[SmartStreamProcessor] No validator, calling FFmpeg callback directly...");
        pid = start_ffmpeg_callback(stream_info_result);
        LOG_INFO("[SmartStreamProcessor] FFmpeg callback returned PID: {}", pid);
        if (pid > 0) {
            status = GatewayStatus::Running;
            return true;
        } else {
            status = GatewayStatus::Error;
            return false;
        }
    }
    
    LOG_INFO("[SmartStreamProcessor] Using StreamStartValidator to validate FFmpeg start");
    // 使用 StreamStartValidator 验证
    // 注意：StreamStartValidator 的 ValidateSimple 方法需要更新以支持 StreamInfoResult
    // 暂时使用包装函数
    auto wrapped_callback = [start_ffmpeg_callback, stream_info_result]() {
        LOG_INFO("[SmartStreamProcessor] Wrapped callback invoked, calling actual FFmpeg start...");
        pid_t result = start_ffmpeg_callback(stream_info_result);
        LOG_INFO("[SmartStreamProcessor] Wrapped callback returned PID: {}", result);
        return result;
    };
    
    LOG_INFO("[SmartStreamProcessor] Calling validator->ValidateSimple...");
    bool success = stream_start_validator_->ValidateSimple(
        pid,
        status,
        target_app,
        target_stream,
        wrapped_callback,
        read_ffmpeg_error_callback);
    
    LOG_INFO("[SmartStreamProcessor] ValidateSimple returned: {} (PID: {}, Status: {})", 
            success, pid, static_cast<int>(status));
    
    return success;
}

} // namespace utils
} // namespace gateway

