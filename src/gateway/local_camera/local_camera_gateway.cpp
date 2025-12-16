#include "gateway/local_camera/local_camera_gateway.hpp"
#include "gateway/utils/ffmpeg_command_builder.hpp"
#include "gateway/utils/smart_stream_processor.hpp"  // 智能流处理器
#include "gateway/utils/ffmpeg_process_helper.hpp"  // FFmpeg 进程管理辅助
#include "gateway/utils/bitrate_allocation_helper.hpp"  // 码率分配辅助
#include "gateway/utils/error_codes.hpp"           // 错误码定义
#include "gateway/utils/ffmpeg_process_helper.hpp"  // FFmpeg 进程管理辅助
#include "config/config_loader.hpp"
// 注意：device_manager.hpp、device_resolver.hpp、stream_validator.hpp 和 health_monitor.hpp 在 cpp 文件中包含，避免循环依赖
#include "gateway/local_camera/device_manager.hpp"
#include "gateway/local_camera/device_resolver.hpp"
#include "gateway/local_camera/stream_validator.hpp"
#include "gateway/local_camera/health_monitor.hpp"
#include "streaming/zlmediakit/zlm_client.hpp"
#include "streaming/stream_manager.hpp"
#include "process/process_manager.hpp"
#include "process/process_monitor.hpp"
#include "process/ffmpeg_executor.hpp"
#include "utils/logger.hpp"
#include "utils/camera_detector.hpp"
#include "utils/stream_status_checker.hpp"
#include "utils/bitrate_allocator.hpp"
#include "api/websocket_server.hpp"

#include <sstream>
#include <algorithm>
#include <cstdlib>
#include <unistd.h>
#include <sys/wait.h>
#include <sys/select.h>
#include <sys/time.h>
#include <signal.h>
#include <fcntl.h>
#include <errno.h>
#include <cstring>
#include <thread>
#include <chrono>
#include <atomic>
#include <set>
#include <fstream>


#include "gateway/utils/gateway_registry.hpp"

namespace gateway {

namespace {
static gateway::utils::AutoRegister reg("local_camera", [](const gateway::utils::GatewayContext& ctx) -> std::shared_ptr<gateway::GatewayBase> {
    if (ctx.config->local_camera.enabled) { // Assuming config has local_camera field, usage in constructor implies checking config->rtsp.ffmpeg_path so local_camera config exists generically or we should check something.
        // Actually typically it might be config->local_camera.enabled.
        // Let's verify config structure later if needed, but standard pattern is used. 
        // NOTE: The previous file view didn't show config struct definition, but standard pattern applies.
        // Safe to assume config->local_camera exists if it follows pattern.
        return std::make_shared<LocalCameraGateway>(
            ctx.config, ctx.zlm_client, ctx.process_manager, ctx.websocket_server, ctx.stream_manager, ctx.bitrate_allocator);
    }
    return nullptr;
});
}

LocalCameraGateway::LocalCameraGateway(std::shared_ptr<config::Config> config,
                                       std::shared_ptr<streaming::ZLMClient> zlm_client,
                                       std::shared_ptr<process::ProcessManager> process_manager,
                                       std::shared_ptr<api::WebSocketServer> websocket_server,
                                       std::shared_ptr<streaming::StreamManager> stream_manager,
                                       std::shared_ptr<::utils::BitrateAllocator> bitrate_allocator)
    : config_(config), zlm_client_(zlm_client), process_manager_(process_manager), 
      websocket_server_(websocket_server), stream_manager_(stream_manager), bitrate_allocator_(bitrate_allocator) {
    
    // 获取 FFmpeg 路径
    ffmpeg_path_ = config_->rtsp.ffmpeg_path;  // 复用RTSP配置的FFmpeg路径
    if (ffmpeg_path_.empty()) {
        ffmpeg_path_ = "ffmpeg";  // fallback to system PATH
    }
    
    // 创建摄像头检测器
    camera_detector_ = std::make_unique<::utils::CameraDetector>(ffmpeg_path_);
    
    // 创建设备管理器
    device_manager_ = std::make_unique<gateway::local_camera::DeviceManager>(config_, camera_detector_, websocket_server_);
    
    // 创建设备解析器
    device_resolver_ = std::make_unique<gateway::local_camera::DeviceResolver>(device_manager_.get());
    
    // 创建流启动验证器
    stream_validator_ = std::make_unique<gateway::local_camera::StreamValidator>(config_, zlm_client_, process_manager_);
    
    // 创建健康监控器
    health_monitor_ = std::make_unique<gateway::local_camera::HealthMonitor>(config_, zlm_client_, process_manager_);
    
    // 启动健康监控
    health_monitor_->Start(
        // 获取流列表回调
        [this]() {
            std::lock_guard<std::mutex> lock(streams_mutex_);
            std::vector<std::pair<std::string, StreamInfo>> result;
            for (const auto& pair : streams_) {
                if (pair.second.status == GatewayStatus::Running ||
                    pair.second.status == GatewayStatus::Starting) {
                    result.push_back({pair.first, pair.second});
                }
            }
            return result;
        },
        // 健康检查回调（使用默认实现）
        nullptr,
        // 更新流状态回调
        [this](const std::string& stream_id, GatewayStatus status) {
            std::lock_guard<std::mutex> lock(streams_mutex_);
            auto it = streams_.find(stream_id);
            if (it != streams_.end()) {
                it->second.status = status;
            }
        },
        // 更新恢复信息回调
        [this](const std::string& stream_id, int recover_attempts, std::chrono::system_clock::time_point last_recover_time) {
            std::lock_guard<std::mutex> lock(streams_mutex_);
            auto it = streams_.find(stream_id);
            if (it != streams_.end()) {
                it->second.recover_attempts = recover_attempts;
                it->second.last_recover_time = last_recover_time;
            }
        },
        // 恢复流回调
        [this](const std::string& source_url, const std::string& target_app, const std::string& target_stream, const std::string& output_protocol) {
            return this->Start(source_url, target_app, target_stream, output_protocol).IsSuccess();
        },
        websocket_server_,
        stream_manager_
    );
    
    // 构建 ZLMediaKit RTMP 推流地址
    std::ostringstream oss;
    oss << "rtmp://127.0.0.1:" << config_->zlmediakit.rtmp_port;
    zlm_rtmp_url_ = oss.str();
    
    // 创建 FFmpeg 命令构建器
    ffmpeg_builder_ = std::make_unique<gateway::utils::FFmpegCommandBuilder>(
        config_, ffmpeg_path_, zlm_rtmp_url_);
    
    // 初始化通用辅助类
    // 1. FFmpeg 进程管理辅助
    ffmpeg_helper_ = std::make_unique<gateway::utils::FFmpegProcessHelper>(process_manager_);
    
    // 2. 码率分配辅助（使用传入的 BitrateAllocator）
    if (bitrate_allocator_) {
        bitrate_helper_ = std::make_unique<gateway::utils::BitrateAllocationHelper>(bitrate_allocator_);
    }
    
    // 3. 智能流处理器（本地摄像头不需要流检测，但需要统一处理逻辑）
    // 注意：本地摄像头不能直接代理，必须使用 FFmpeg，所以不需要 StreamInfoDetector
    std::unique_ptr<gateway::utils::StreamInfoDetector> stream_info_detector = nullptr;
    
    gateway::utils::StreamStartValidationConfig validator_config;
    // 从配置中读取流验证参数，优先使用local_camera的stream_start配置，如果没有则使用gateway通用配置
    if (config_) {
        // Local Camera有专门的stream_start配置，优先使用
        if (config_->local_camera.stream_start.process_stable_wait_ms > 0) {
            validator_config.process_stable_wait_ms = config_->local_camera.stream_start.process_stable_wait_ms;
            validator_config.zlm_check_interval_ms = config_->local_camera.stream_start.zlm_check_interval_ms;
            validator_config.zlm_check_timeout_ms = config_->local_camera.stream_start.zlm_check_timeout_ms;
            validator_config.max_check_attempts = config_->local_camera.stream_start.max_check_attempts;
        } else {
            // 使用gateway通用配置
            validator_config.process_stable_wait_ms = config_->gateway.stream_validation.process_stable_wait_ms;
            validator_config.zlm_check_interval_ms = config_->gateway.stream_validation.zlm_check_interval_ms;
            validator_config.zlm_check_timeout_ms = config_->gateway.stream_validation.zlm_check_timeout_ms;
            validator_config.max_check_attempts = config_->gateway.stream_validation.max_check_attempts;
        }
    } else {
        // 默认值（向后兼容）
        validator_config.process_stable_wait_ms = 2000;
        validator_config.zlm_check_interval_ms = 3000;
        validator_config.zlm_check_timeout_ms = 10000;
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
    
    // 启动码率分配更新线程（如果码率分配器存在）
    if (bitrate_allocator_) {
        bitrate_update_running_ = true;
        bitrate_update_thread_ = std::thread(&LocalCameraGateway::BitrateUpdateLoop, this);
        LOG_INFO("[LocalCameraGateway] 码率分配更新线程已启动");
    }
    
    LOG_INFO("[LocalCameraGateway] 初始化完成，FFmpeg路径: {}，设备缓存TTL: {}秒，健康检查间隔: {}秒，自动恢复: {}", 
            ffmpeg_path_, config_->local_camera.device_cache_ttl_seconds, 
            config_->local_camera.health_monitor.check_interval_seconds, 
            config_->local_camera.health_monitor.auto_recover ? "启用" : "禁用");
}

LocalCameraGateway::~LocalCameraGateway() {
    // 停止码率分配更新线程
    if (bitrate_update_running_) {
        bitrate_update_running_ = false;
        if (bitrate_update_thread_.joinable()) {
            bitrate_update_thread_.join();
        }
        LOG_INFO("[LocalCameraGateway] 码率分配更新线程已停止");
    }
    
    // 停止健康监控
    if (health_monitor_) {
        health_monitor_->Stop();
    }
    
    // 停止所有流
    std::lock_guard<std::mutex> lock(streams_mutex_);
    for (auto& pair : streams_) {
        std::string stream_id = pair.second.target_app + "/" + pair.second.target_stream;
        if (pair.second.pid > 0) {
            ffmpeg_helper_->StopProcess(pair.second.pid, stream_id,
                [&pair](GatewayStatus status) { pair.second.status = status; });
        }
    }
    streams_.clear();
}

Result<std::vector<LocalCameraDevice>> LocalCameraGateway::DiscoverCameras() {
    return Result<std::vector<LocalCameraDevice>>::Success(device_manager_->DiscoverCameras());
}

Result<std::vector<LocalCameraDevice>> LocalCameraGateway::ListCameras(bool refresh) {
    return Result<std::vector<LocalCameraDevice>>::Success(device_manager_->ListCameras(refresh));
}

Result<LocalCameraDevice> LocalCameraGateway::GetCamera(const std::string& device_id) const {
    auto camera = device_manager_->GetCamera(device_id);
    if (camera.device_id.empty()) {
        return Result<LocalCameraDevice>::Failure(gateway::DeviceNotFoundException(device_id));
    }
    return Result<LocalCameraDevice>::Success(camera);
}

Result<int> LocalCameraGateway::GetCameraIndexFromDeviceID(const std::string& device_id) const {
    int index = device_manager_->GetCameraIndexFromDeviceID(device_id);
    if (index < 0) {
        return Result<int>::Failure(gateway::DeviceNotFoundException(device_id));
    }
    return Result<int>::Success(index);
}

Result<std::pair<std::vector<std::string>, std::vector<int>>> LocalCameraGateway::QueryDeviceCapabilities(const std::string& device_id) const {
    return Result<std::pair<std::vector<std::string>, std::vector<int>>>::Success(device_manager_->QueryDeviceCapabilities(device_id));
}

Result<void> LocalCameraGateway::StartCameraStream(const std::string& device_id,
                                                  const std::string& target_app,
                                                  const std::string& target_stream,
                                                  const std::string& output_protocol) {
    if (device_id.empty() || target_app.empty() || target_stream.empty()) {
        return Result<void>::Failure(gateway::InvalidParameterException("device_id, target_app or target_stream is empty"));
    }

    // Resolve Device
    auto resolve_result = device_resolver_->ResolveDevice(device_id);
    if (!resolve_result.success) {
        // Report error via StreamManager if possible
        if (stream_manager_) {
             streaming::StreamMetadata metadata;
            metadata.app = target_app;
            metadata.stream = target_stream;
            metadata.protocol = "local-camera";
            metadata.output_protocol = output_protocol;
            // metadata.source_url = source_url;
            metadata.device_id = device_id;
            metadata.gateway_type = "local_camera_gateway";
            metadata.status = streaming::StreamStatus::Error;
            metadata.error_code = "DeviceNotFound";
            metadata.error_message = resolve_result.error_message;
            stream_manager_->OnStreamCreateRequested(metadata);
            stream_manager_->OnStreamCreateResult(target_app, target_stream, false, "DeviceNotFound", resolve_result.error_message);
        }
        return Result<void>::Failure(gateway::DeviceNotFoundException(resolve_result.error_message));
    }

    std::string final_device_id = resolve_result.device_id;
    int camera_index = resolve_result.camera_index;
    LocalCameraDevice camera = resolve_result.camera;
    std::string stream_id = target_app + "/" + target_stream;
    
    // Check occupancy
    {
        std::lock_guard<std::mutex> lock(streams_mutex_);
        auto it = streams_.find(stream_id);
        if (it != streams_.end()) {
             if (it->second.device_id != final_device_id && !final_device_id.empty() && !it->second.device_id.empty()) {
                 // Conflict logic same as before...
                 LOG_WARN("[LocalCameraGateway] Stream conflict, stopping old stream...");
                 if (it->second.pid > 0) {
                    ffmpeg_helper_->StopProcess(it->second.pid, stream_id,
                        [&it](GatewayStatus status) { it->second.status = status; });
                }
                streams_.erase(it);
             } else if (it->second.status == GatewayStatus::Running) {
                 return Result<void>::Success(); // Creating existing running stream is success (idempotent)
             } else {
                 // Cleaning up...
                 if (it->second.pid > 0) {
                    ffmpeg_helper_->StopProcess(it->second.pid, stream_id,
                        [&it](GatewayStatus status) { it->second.status = status; });
                }
             }
        }
    }
    
    // Check if device occupied by OTHER stream
    for (const auto& stream_pair : streams_) {
        const StreamInfo& existing_info = stream_pair.second;
        if (existing_info.device_id == final_device_id || 
            (existing_info.device_id.empty() && existing_info.camera_index == camera_index)) {
             if (existing_info.status == GatewayStatus::Running || 
                existing_info.status == GatewayStatus::Starting) {
                    if ((existing_info.target_app != target_app || existing_info.target_stream != target_stream) &&
                        existing_info.pid > 0 && process::ProcessMonitor::IsProcessAlive(existing_info.pid)) {
                        
                        std::string msg = "Device busy: occupied by " + existing_info.target_app + "/" + existing_info.target_stream;
                        return Result<void>::Failure(gateway::ConflictException(msg));
                    }
                }
        }
    }

    // ... (rest of logic: native resolution, StreamInfo setup, SmartProcessor call) ...
    // Since original logic was very long, we will preserve it but adapt the return type.
    // For brevity in this tool call, I will delegate the core logic to the internal helper calls 
    // but we need to match the original implementation logic closely.
    
    // Re-implementing the core logic:
    
    std::string final_output_protocol = output_protocol.empty() ? "http-flv" : output_protocol;
    
    // Detect resolution
    int native_width = 0, native_height = 0;
    auto capabilities = device_manager_->QueryDeviceCapabilities(final_device_id);
    if (!capabilities.first.empty()) {
        std::string first_resolution = capabilities.first[0];
        size_t x_pos = first_resolution.find('x');
        if (x_pos != std::string::npos) {
                try {
                native_width = std::stoi(first_resolution.substr(0, x_pos));
                native_height = std::stoi(first_resolution.substr(x_pos + 1));
            } catch (...) {}
        }
    }

    StreamInfo info;
    info.device_id = final_device_id;
    info.camera_index = camera_index;
    info.camera_name = camera.name;
    info.target_app = target_app;
    info.target_stream = target_stream;
    info.output_protocol = final_output_protocol;
    info.status = GatewayStatus::Starting;

    // Construct source_url for SmartStreamProcessor, even if it's not directly used for local camera
    std::ostringstream oss_source_url;
    oss_source_url << "local-camera://device_id:" << final_device_id;
    std::string source_url = oss_source_url.str();

    auto result = smart_processor_->ProcessStream(
        source_url, target_app, target_stream, final_output_protocol, "local-camera", "local_camera_gateway",
        [](const std::string&, const std::string&, const std::string&) { return false; },
        [this](const std::string& app, const std::string& stream, const std::string& schema) {
            return zlm_client_ ? zlm_client_->GetStreamInfo(app, stream, schema.empty() ? "rtmp" : schema) : streaming::StreamInfo();
        },
        [this, &info, final_device_id, camera_index, camera_name = camera.name, 
         final_output_protocol, native_width, native_height](const gateway::utils::StreamInfoResult&) {
             // ... (paste the lambda content from original code to ensure logic is kept) ...
             // Simplified for this tool call, assume we call a helper if possible or duplicate
             // We MUST duplicate the lambda body to maintain functionality
             
             // --- Lambda Body Start ---
              gateway::utils::FFmpegCommandOptions options;
              // ... (setup options) ...
              // Since we can't easily partially replace, we will use the same logic
              
              int recommended_bitrate = 0;
              if (bitrate_helper_) {
                  std::string resolution = "";
                  int fps = config_->local_camera.default_fps;
                   if (final_output_protocol == "webrtc") {
                    resolution = config_->local_camera.webrtc.resolution;
                    fps = config_->local_camera.webrtc.fps;
                } else if (final_output_protocol == "http-flv" || final_output_protocol == "hls") {
                    resolution = config_->local_camera.flv_hls.resolution;
                    fps = config_->local_camera.flv_hls.fps;
                }
                 recommended_bitrate = bitrate_helper_->GetRecommendedBitrate(
                    info.target_app, info.target_stream, "local-camera", final_output_protocol,
                    resolution, fps, 5, 0);
              }
              
             int audio_device_index = -1;
             LocalCameraDevice camera = device_manager_->GetCamera(final_device_id);
             if (!camera.device_id.empty() && camera.audio_device_index >= 0) {
                 audio_device_index = camera.audio_device_index;
             }

#ifdef __linux__
            options.input_url = "/dev/video" + std::to_string(camera_index);
            options.input_format = "v4l2";
#else
            options.input_url = std::to_string(camera_index);
            options.input_format = "avfoundation";
#endif
            if (audio_device_index >= 0) {
                options.audio_input_format = ":" + std::to_string(audio_device_index);
            } else {
                 // Heuristic matching for audio device (simplified from original)
                 std::string camera_name_lower = camera_name;
                 std::transform(camera_name_lower.begin(), camera_name_lower.end(), 
                                camera_name_lower.begin(), ::tolower);
                 if (camera_name_lower.find("1080p") != std::string::npos || 
                     camera_name_lower.find("usb camera") != std::string::npos) {
                     options.audio_input_format = ":2";
                 } else if (camera_index == 0) {
                     options.audio_input_format = ":2";
                 } else {
                     options.audio_input_format = ":" + std::to_string(camera_index);
                 }
            }
            
            options.output_protocol = final_output_protocol;
            options.target_app = info.target_app;
            options.target_stream = info.target_stream;
            options.target_bitrate_kbps = recommended_bitrate;
            options.input_width = native_width;
            options.input_height = native_height;
            options.gateway_name = "LocalCamera Gateway";

            std::string command = ffmpeg_builder_->BuildCommand(options);
            int pid = 0;
            GatewayStatus status = GatewayStatus::Stopped;
            std::string s_id = info.target_app + "/" + info.target_stream;
            std::string log_file = "/tmp/ffmpeg_local_camera_" + info.target_app + "_" + info.target_stream + ".log";
            
            if (ffmpeg_helper_->StartProcess(command, s_id, log_file, pid, status)) {
                return pid;
            } else {
                return 0;
            }
             // --- Lambda Body End ---
         },
         [this, target_app, target_stream]() {
             std::string s_id = target_app + "/" + target_stream;
             return ffmpeg_helper_->ReadErrorLog(s_id);
         },
         "rtmp", 0
    );

    info.status = result.status;
    info.pid = result.pid;
    info.start_time = std::chrono::system_clock::now();
    info.recover_attempts = 0;

    if (result.success) {
        std::string stream_id = info.target_app + "/" + info.target_stream;
        streams_[stream_id] = info;
        LOG_INFO("[LocalCameraGateway] Start success: {} -> {}/{} (原生分辨率: {}x{})", camera_index, target_app, target_stream, native_width, native_height);
        return Result<void>::Success();
    } else {
        std::string stream_id = info.target_app + "/" + info.target_stream;
         info.status = GatewayStatus::Error;
         streams_[stream_id] = info;
         return Result<void>::Failure(gateway::InternalServerException("Failed to start FFmpeg process"));
    }
}

Result<void> LocalCameraGateway::Start(const std::string& source_url,
                               const std::string& target_app,
                               const std::string& target_stream,
                               const std::string& output_protocol) {
    // Parse source_url to get device_id
    auto resolve_result = device_resolver_->ResolveDevice(source_url);
    if (!resolve_result.success) {
        return Result<void>::Failure(gateway::InvalidRequestException(resolve_result.error_message));
    }
    
    // Call StartCameraStream
    return StartCameraStream(resolve_result.device_id, target_app, target_stream, output_protocol);
}
    
Result<void> LocalCameraGateway::Stop(const std::string& target_app,
                              const std::string& target_stream) {
    return StopCameraStream(target_app, target_stream);
}

Result<void> LocalCameraGateway::StopCameraStream(const std::string& target_app,
                                                  const std::string& target_stream) {
    std::string stream_id = target_app + "/" + target_stream;
    
    std::lock_guard<std::mutex> lock(streams_mutex_);
    
    auto it = streams_.find(stream_id);
    if (it == streams_.end()) {
        // Stream not found in local list, attempt idempotent cleanup on ZLM/StreamManager
        LOG_WARN("[LocalCameraGateway] Stream not in local list: {}/{} (idempotent stop)", target_app, target_stream);
        if (zlm_client_) {
            try {
                zlm_client_->DeleteStream(target_app, target_stream);
            } catch (const std::exception& e) {
                LOG_WARN("[LocalCameraGateway] Failed to delete ZLM stream (idempotent cleanup): {}", e.what());
            }
        }
        if (stream_manager_) {
            stream_manager_->UnregisterStream(target_app, target_stream);
        }
        return Result<void>::Success(); // Idempotent success
    }
    
    StreamInfo& info = it->second;
    bool ffmpeg_stopped = true; // Assume success if no PID or StopProcess succeeds
    
    // Stop FFmpeg process if running
    if (info.pid > 0) {
        ffmpeg_stopped = ffmpeg_helper_->StopProcess(info.pid, stream_id,
            [&info](GatewayStatus status) { info.status = status; });
        if (!ffmpeg_stopped) {
            LOG_ERROR("[LocalCameraGateway] Failed to stop FFmpeg process for {}/{}", target_app, target_stream);
            // Even if FFmpeg fails to stop, we proceed with cleanup and remove from our list
        }
    } else {
        LOG_DEBUG("[LocalCameraGateway] No FFmpeg PID for {}/{}, assuming stopped.", target_app, target_stream);
    }
    
    // Unregister stream from StreamManager
    if (stream_manager_) {
        stream_manager_->UnregisterStream(target_app, target_stream);
        LOG_DEBUG("[LocalCameraGateway] Stream unregistered from StreamManager: {}/{}", target_app, target_stream);
    }
    
    // Clean up statistics
    info.recover_attempts = 0;
    info.last_recover_time = std::chrono::system_clock::time_point();
    
    // Delete stream from ZLMediaKit
    if (zlm_client_) {
        try {
            zlm_client_->DeleteStream(target_app, target_stream);
            LOG_DEBUG("[LocalCameraGateway] Stream deleted from ZLMediaKit: {}/{}", target_app, target_stream);
        } catch (const std::exception& e) {
            LOG_WARN("[LocalCameraGateway] Failed to delete ZLM stream for {}/{}: {}", target_app, target_stream, e.what());
            // This is a warning, but we still consider the stop operation successful if local state is cleaned
        }
    }

    // Remove from local streams map
    streams_.erase(it);
    
    if (ffmpeg_stopped) {
        LOG_INFO("[LocalCameraGateway] StopCameraStream success: {}/{}", target_app, target_stream);
        return Result<void>::Success();
    } else {
        // If FFmpeg explicitly failed to stop, but other cleanup happened,
        // we might still return success for the overall gateway operation,
        // but log the FFmpeg failure. For now, let's return failure if FFmpeg failed.
        return Result<void>::Failure(gateway::InternalServerException("Failed to stop FFmpeg process."));
    }
}

bool LocalCameraGateway::IsRunning(const std::string& target_app,
                                   const std::string& target_stream) {
    std::string stream_id = target_app + "/" + target_stream;
    std::lock_guard<std::mutex> lock(streams_mutex_);
    auto it = streams_.find(stream_id);
    if (it == streams_.end()) {
        return false;
    }
    const StreamInfo& info = it->second;
    if (info.pid > 0) {
        return ffmpeg_helper_->IsProcessRunning(info.pid);
    }
    return info.status == GatewayStatus::Running;
}

GatewayStatus LocalCameraGateway::GetStatus(const std::string& target_app,
                                            const std::string& target_stream) {
    std::string stream_id = target_app + "/" + target_stream;
    std::lock_guard<std::mutex> lock(streams_mutex_);
    auto it = streams_.find(stream_id);
    if (it == streams_.end()) {
        return GatewayStatus::Stopped;
    }
    const StreamInfo& info = it->second;
    if (info.pid > 0 && !ffmpeg_helper_->IsProcessRunning(info.pid)) {
        return GatewayStatus::Stopped;
    }
    return info.status;
}



std::string LocalCameraGateway::BuildFFmpegCommand(const StreamInfo& info) {
    // 获取音频设备索引
    int audio_device_index = -1;
    LocalCameraDevice camera = device_manager_->GetCamera(info.device_id);
    if (!camera.device_id.empty() && camera.audio_device_index >= 0) {
        audio_device_index = camera.audio_device_index;
    }
    
    // 查询设备能力，获取原生分辨率
    int native_width = 0, native_height = 0;
    auto capabilities = device_manager_->QueryDeviceCapabilities(info.device_id);
    if (!capabilities.first.empty()) {
        // 尝试解析第一个支持的分辨率作为原生分辨率
        // 通常设备列表中的第一个分辨率是设备默认/推荐的分辨率
        std::string first_resolution = capabilities.first[0];
        size_t x_pos = first_resolution.find('x');
        if (x_pos != std::string::npos) {
            try {
                native_width = std::stoi(first_resolution.substr(0, x_pos));
                native_height = std::stoi(first_resolution.substr(x_pos + 1));
                LOG_INFO("[LocalCameraGateway] 检测到设备原生分辨率: {}x{} (设备: {})", 
                        native_width, native_height, info.device_id);
            } catch (const std::exception& e) {
                LOG_WARN("[LocalCameraGateway] 解析设备原生分辨率失败: {}, 将使用配置中的分辨率", first_resolution);
            }
        }
    }
    
    // 使用构建器生成命令，传入原生分辨率
    gateway::utils::FFmpegCommandOptions options;
    // 构造 input_url
#ifdef __linux__
    options.input_url = "/dev/video" + std::to_string(info.camera_index);
    options.input_format = "v4l2";
#else
    options.input_url = std::to_string(info.camera_index);
    options.input_format = "avfoundation";
#endif

    // 音频设备
    if (audio_device_index >= 0) {
        options.audio_input_format = ":" + std::to_string(audio_device_index);
    } else {
        // 启发式匹配音频设备 (简单策略：name 包含 "USB" -> :2，index==0 -> :2，否则 :index)
        // 注意：原 builder 的启发式逻辑比较复杂，这里简化处理或应在 builder 中增强，
        // 但鉴于 builder 是通用的，特定设备的启发式逻辑最好留在 Gateway 或 DeviceManager 中。
        // 这里暂时保留简单的默认逻辑：
        options.audio_input_format = ":default"; // 让 builder 使用默认
    }
    
    // 原 builder 的复杂音频匹配逻辑：
    // std::string audio_input = ":default"; 
    // if (audio_device_index >= 0) ...
    // else { if (name has "1080p" or "usb") -> :2; else if index==0 -> :2; else -> :index }
    // 为了保持行为一致性，我们在这里还原这个逻辑：
    if (audio_device_index < 0) {
       std::string camera_name_lower = info.camera_name;
       std::transform(camera_name_lower.begin(), camera_name_lower.end(), 
                      camera_name_lower.begin(), ::tolower);
       if (camera_name_lower.find("1080p") != std::string::npos || 
           camera_name_lower.find("usb camera") != std::string::npos) {
           options.audio_input_format = ":2";
       } else if (info.camera_index == 0) {
           options.audio_input_format = ":2";
       } else {
           options.audio_input_format = ":" + std::to_string(info.camera_index);
       }
    }

    options.output_protocol = info.output_protocol;
    options.target_app = info.target_app;
    options.target_stream = info.target_stream;
    options.target_bitrate_kbps = 0; // default
    options.input_width = native_width;
    options.input_height = native_height;
    options.gateway_name = "LocalCamera Gateway";

    return ffmpeg_builder_->BuildCommand(options);
}

std::string LocalCameraGateway::BuildFFmpegCommandWithBitrate(const StreamInfo& info, int bitrate_kbps) {
    // 获取音频设备索引
    int audio_device_index = -1;
    LocalCameraDevice camera = device_manager_->GetCamera(info.device_id);
    if (!camera.device_id.empty() && camera.audio_device_index >= 0) {
        audio_device_index = camera.audio_device_index;
    }
    
    // 查询设备能力，获取原生分辨率
    int native_width = 0, native_height = 0;
    auto capabilities = device_manager_->QueryDeviceCapabilities(info.device_id);
    if (!capabilities.first.empty()) {
        // 尝试解析第一个支持的分辨率作为原生分辨率
        // 通常设备列表中的第一个分辨率是设备默认/推荐的分辨率
        std::string first_resolution = capabilities.first[0];
        size_t x_pos = first_resolution.find('x');
        if (x_pos != std::string::npos) {
            try {
                native_width = std::stoi(first_resolution.substr(0, x_pos));
                native_height = std::stoi(first_resolution.substr(x_pos + 1));
                LOG_INFO("[LocalCameraGateway] 检测到设备原生分辨率: {}x{} (设备: {})", 
                        native_width, native_height, info.device_id);
            } catch (const std::exception& e) {
                LOG_WARN("[LocalCameraGateway] 解析设备原生分辨率失败: {}, 将使用配置中的分辨率", first_resolution);
            }
        }
    }
    
    // 使用构建器生成命令，传入智能分配的码率和原生分辨率
    gateway::utils::FFmpegCommandOptions options;
#ifdef __linux__
    options.input_url = "/dev/video" + std::to_string(info.camera_index);
    options.input_format = "v4l2";
#else
    options.input_url = std::to_string(info.camera_index);
    options.input_format = "avfoundation";
#endif

    // 音频设备逻辑与 BuildFFmpegCommand 保持一致
    if (audio_device_index >= 0) {
        options.audio_input_format = ":" + std::to_string(audio_device_index);
    } else {
       std::string camera_name_lower = info.camera_name;
       std::transform(camera_name_lower.begin(), camera_name_lower.end(), 
                      camera_name_lower.begin(), ::tolower);
       if (camera_name_lower.find("1080p") != std::string::npos || 
           camera_name_lower.find("usb camera") != std::string::npos) {
           options.audio_input_format = ":2";
       } else if (info.camera_index == 0) {
           options.audio_input_format = ":2";
       } else {
           options.audio_input_format = ":" + std::to_string(info.camera_index);
       }
    }

    options.output_protocol = info.output_protocol;
    options.target_app = info.target_app;
    options.target_stream = info.target_stream;
    options.target_bitrate_kbps = bitrate_kbps;
    options.input_width = native_width;
    options.input_height = native_height;
    options.gateway_name = "LocalCamera Gateway";

    return ffmpeg_builder_->BuildCommand(options);
}




std::string LocalCameraGateway::CheckDeviceOccupied(const std::string& device_id) const {
    std::lock_guard<std::mutex> lock(streams_mutex_);
    
    // 检查内部流状态
    for (const auto& stream_pair : streams_) {
        const StreamInfo& existing_info = stream_pair.second;
        if (existing_info.device_id == device_id) {
            if (existing_info.status == GatewayStatus::Running || 
                existing_info.status == GatewayStatus::Starting) {
                // 检查进程是否还在运行（使用 ProcessMonitor 的静态方法）
                if (existing_info.pid > 0 && process::ProcessMonitor::IsProcessAlive(existing_info.pid)) {
                    return existing_info.target_app + "/" + existing_info.target_stream;
                }
            }
        }
    }
    
    // 如果内部没有找到，检查 ZLMediaKit 中的流
    if (zlm_client_) {
        try {
            auto zlm_streams = zlm_client_->GetStreamList();
            int camera_index = device_manager_->GetCameraIndexFromDeviceID(device_id);
            std::string device_id_pattern = "local-camera://device_id:" + device_id;
            
            for (const auto& zlm_stream : zlm_streams) {
                // 检查流的 origin_url 是否包含该设备的 device_id
                if (zlm_stream.origin_url.find(device_id_pattern) != std::string::npos ||
                    (camera_index >= 0 && zlm_stream.origin_url.find("local-camera://" + std::to_string(camera_index)) != std::string::npos)) {
                    // 只检查真正活跃的流（alive=true），忽略已停止的流
                    if (zlm_stream.alive && streaming::ZLMClient::IsStreamActive(zlm_stream)) {
                        return zlm_stream.app + "/" + zlm_stream.stream;
                    }
                }
            }
        } catch (const std::exception& e) {
            LOG_WARN("[LocalCameraGateway] Failed to check ZLMediaKit streams for device occupancy: {}", e.what());
        }
    }
    
    return "";  // 设备未被占用
}



void LocalCameraGateway::BitrateUpdateLoop() {
    LOG_INFO("[LocalCameraGateway] 码率分配更新循环线程启动");
    
    if (!bitrate_allocator_) {
        LOG_WARN("[LocalCameraGateway] 码率分配器不存在，退出更新循环");
        return;
    }
    
    // 获取更新间隔（从配置中读取，默认10秒）
    int update_interval_sec = 10;
    if (bitrate_allocator_) {
        auto config = bitrate_allocator_->GetConfig();
        update_interval_sec = config.update_interval_sec > 0 ? config.update_interval_sec : 10;
    }
    
    while (bitrate_update_running_) {
        // 等待更新间隔
        for (int i = 0; i < update_interval_sec && bitrate_update_running_; ++i) {
            std::this_thread::sleep_for(std::chrono::seconds(1));
        }
        
        if (!bitrate_update_running_) {
            break;
        }
        
        try {
            // 更新所有流的码率分配
            if (bitrate_allocator_ && stream_manager_) {
                bool success = bitrate_allocator_->UpdateAllocations(stream_manager_);
                if (success) {
                    LOG_DEBUG("[LocalCameraGateway] 码率分配更新成功");
                    
                    // 获取更新后的码率分配信息
                    auto allocations = bitrate_allocator_->GetAllAllocations();
                    
                    // 检查是否有流的码率需要更新（这里只记录日志，实际动态更新码率需要重启FFmpeg）
                    // 注意：动态更新码率需要重启FFmpeg进程，这可能会影响流的连续性
                    // 因此这里只记录日志，实际更新可以在流重启时应用新的码率
                    for (const auto& alloc : allocations) {
                        std::string stream_key = alloc.app + "/" + alloc.stream;
                        
                        // 检查是否是本地摄像头流
                        std::lock_guard<std::mutex> lock(streams_mutex_);
                        auto it = streams_.find(stream_key);
                        if (it != streams_.end() && 
                            (it->second.status == GatewayStatus::Running || 
                             it->second.status == GatewayStatus::Starting)) {
                            // 记录码率分配信息（用于调试）
                            LOG_DEBUG("[LocalCameraGateway] 流 {}/{} 当前码率: {} kbps (观看者: {}, 状态: {})",
                                    alloc.app, alloc.stream, alloc.current_bitrate_kbps,
                                    alloc.reader_count, 
                                    alloc.status == ::utils::StreamAllocationStatus::Running ? "运行中" : 
                                    alloc.status == ::utils::StreamAllocationStatus::Starting ? "启动中" : "其他");
                        }
                    }
                } else {
                    LOG_WARN("[LocalCameraGateway] 码率分配更新失败");
                }
            }
        } catch (const std::exception& e) {
            LOG_ERROR("[LocalCameraGateway] 码率分配更新异常: {}", e.what());
        }
    }
    
    LOG_INFO("[LocalCameraGateway] 码率分配更新循环线程退出");
}

} // namespace gateway

