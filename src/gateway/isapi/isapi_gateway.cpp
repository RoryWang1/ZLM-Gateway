#include "gateway/isapi/isapi_gateway.hpp"
#include "gateway/utils/gateway_config_helper.hpp"
#include "gateway/utils/smart_stream_processor.hpp"
#include "gateway/utils/ffmpeg_process_helper.hpp"
#include "gateway/utils/bitrate_allocation_helper.hpp"
#include "config/config_loader.hpp"
#include "streaming/zlmediakit/zlm_client.hpp"
#include "streaming/stream_manager.hpp"
#include "utils/logger.hpp"
#include "utils/http_callback.hpp"
#include "utils/url_parser.hpp"
#include "process/process_manager.hpp"
#include "process/ffmpeg_executor.hpp"
#include "utils/zlm_url_builder.hpp"
#include "utils/ffmpeg_params.hpp"
#include "utils/stream_status_checker.hpp"
#include "utils/bitrate_allocator.hpp"
#include <sstream>
#include <algorithm>
#include <curl/curl.h>
#include <regex>
#include <thread>
#include <chrono>


#include "gateway/utils/gateway_registry.hpp"

namespace gateway {

namespace {
static gateway::utils::AutoRegister reg("isapi", [](const gateway::utils::GatewayContext& ctx) -> std::shared_ptr<gateway::GatewayBase> {
    if (ctx.config->isapi.enabled) {
        return std::make_shared<ISAPIGateway>(ctx.config, ctx.zlm_client, ctx.process_manager, ctx.stream_manager);
    }
    return nullptr;
});
}

ISAPIGateway::ISAPIGateway(std::shared_ptr<config::Config> config,
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
    
    // 2. 码率分配辅助（使用默认的 BitrateAllocator）
    ::utils::BitrateAllocatorConfig bitrate_config;
    auto bitrate_allocator = std::make_shared<::utils::BitrateAllocator>(bitrate_config, zlm_client_);
    bitrate_helper_ = std::make_unique<gateway::utils::BitrateAllocationHelper>(bitrate_allocator);
    
    // 3. 智能流处理器
    auto stream_info_detector = std::make_unique<gateway::utils::StreamInfoDetector>(ffprobe_path_);
    
    gateway::utils::StreamStartValidationConfig validator_config;
    // 从配置中读取流验证参数，如果没有配置则使用默认值
    if (config_) {
        validator_config.process_stable_wait_ms = config_->gateway.stream_validation.process_stable_wait_ms;
        validator_config.zlm_check_interval_ms = config_->gateway.stream_validation.zlm_check_interval_ms;
        validator_config.zlm_check_timeout_ms = config_->gateway.stream_validation.zlm_check_timeout_ms;
        validator_config.max_check_attempts = config_->gateway.stream_validation.max_check_attempts;
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
    
    LOG_INFO("ISAPI Gateway 初始化完成（支持直接代理和转码）");
}

ISAPIGateway::~ISAPIGateway() {
    // 停止所有流
    GatewayBase::StopAllStreamsWithFFmpeg<StreamInfo>(
        streams_, streams_mutex_,
        [this](StreamInfo& info) { 
            std::string stream_id = GenerateStreamId(info.target_app, info.target_stream);
            if (info.pid > 0) {
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

Result<void> ISAPIGateway::Start(const std::string& source_url,
                        const std::string& target_app,
                        const std::string& target_stream,
                        const std::string& /* output_protocol */) {
    // source_url 格式: isapi://device_id[/channel_id]
    // 例如: isapi://device_123 或 isapi://device_123/1
    
    if (!::utils::url_parser::ValidateURL(source_url, "isapi://")) {
        return Result<void>::Failure(gateway::InvalidParameterException("Invalid source URL format: " + source_url));
    }
    
    auto [device_id, channel_id] = ::utils::url_parser::ParseDeviceURL(source_url, "isapi://");
    
    return StartDeviceStream(device_id, channel_id, target_app, target_stream);
}

Result<void> ISAPIGateway::Stop(const std::string& target_app,
                       const std::string& target_stream) {
    return StopDeviceStream(target_app, target_stream);
}

bool ISAPIGateway::IsRunning(const std::string& target_app,
                            const std::string& target_stream) {
    std::string stream_id = GenerateStreamId(target_app, target_stream);
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

GatewayStatus ISAPIGateway::GetStatus(const std::string& target_app,
                                     const std::string& target_stream) {
    std::string stream_id = GenerateStreamId(target_app, target_stream);
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

std::vector<ISAPIDevice> ISAPIGateway::DiscoverDevices(const std::string& ip_range,
                                                       int port,
                                                       int timeout_seconds) {
    LOG_INFO("开始发现 ISAPI 设备，超时时间: {} 秒", timeout_seconds);
    
    auto devices = DiscoverDevicesHTTP(ip_range, port, timeout_seconds);
    
    std::lock_guard<std::mutex> lock(devices_mutex_);
    auto now = std::chrono::system_clock::now();
    for (auto& device : devices) {
        device.last_seen = now;
        devices_[device.id] = device;
    }
    
    LOG_INFO("发现 {} 个 ISAPI 设备，已保存到设备列表", devices.size());
    return devices;
}

std::vector<ISAPIDevice> ISAPIGateway::ListDevices() const {
    std::lock_guard<std::mutex> lock(devices_mutex_);
    
    std::vector<ISAPIDevice> result;
    result.reserve(devices_.size());
    for (const auto& pair : devices_) {
        result.push_back(pair.second);
    }
    
    return result;
}

ISAPIDevice ISAPIGateway::GetDevice(const std::string& device_id) const {
    std::lock_guard<std::mutex> lock(devices_mutex_);
    
    auto it = devices_.find(device_id);
    if (it == devices_.end()) {
        return ISAPIDevice();  // 返回空对象
    }
    
    return it->second;
}

std::string ISAPIGateway::AddDevice(const ISAPIDevice& device) {
    std::lock_guard<std::mutex> lock(devices_mutex_);
    
    std::string device_id = device.id;
    if (device_id.empty()) {
        device_id = GenerateDeviceID(device.base_url);
    }
    
    ISAPIDevice device_copy = device;
    device_copy.id = device_id;
    device_copy.last_seen = std::chrono::system_clock::now();
    
    devices_[device_id] = device_copy;
    
    LOG_INFO("手动添加设备: {} ({})", device_id, device.base_url);
    return device_id;
}

bool ISAPIGateway::RemoveDevice(const std::string& device_id) {
    std::lock_guard<std::mutex> lock(devices_mutex_);
    
    auto it = devices_.find(device_id);
    if (it == devices_.end()) {
        LOG_WARN("删除设备失败，设备不存在: {}", device_id);
        return false;
    }
    
    devices_.erase(it);
    LOG_INFO("删除设备: {}", device_id);
    return true;
}

bool ISAPIGateway::UpdateDeviceCredentials(const std::string& device_id,
                                          const std::string& username,
                                          const std::string& password) {
    std::lock_guard<std::mutex> lock(devices_mutex_);
    
    auto it = devices_.find(device_id);
    if (it == devices_.end()) {
        LOG_ERROR("设备不存在: {}", device_id);
        return false;
    }
    
    it->second.username = username;
    it->second.password = password;
    it->second.rtsp_urls.clear();
    
    LOG_INFO("更新设备认证信息: {}", device_id);
    return true;
}

std::vector<std::string> ISAPIGateway::GetDeviceRTSPURLs(const std::string& device_id,
                                                         const std::string& channel_id) {
    ISAPIDevice device = GetDevice(device_id);
    if (device.id.empty()) {
        LOG_ERROR("设备不存在: {}", device_id);
        return {};
    }
    
    auto now = std::chrono::system_clock::now();
    const int RTSP_URL_CACHE_SECONDS = 300;  // 5 分钟缓存
    
    if (!device.rtsp_urls.empty() && now < device.rtsp_urls_expire_time) {
        return device.rtsp_urls;
    }
    
    std::vector<std::string> rtsp_urls;
    
    if (channel_id.empty()) {
        // 获取所有通道的 RTSP 地址
        std::vector<std::string> channels = GetChannels(device);
        for (const auto& ch_id : channels) {
            std::string url = GetStreamURI(device, ch_id);
            if (!url.empty()) {
                rtsp_urls.push_back(url);
            }
        }
    } else {
        // 获取指定通道的 RTSP 地址
        std::string url = GetStreamURI(device, channel_id);
        if (!url.empty()) {
            rtsp_urls.push_back(url);
        }
    }
    
    // 更新缓存
    if (!rtsp_urls.empty()) {
        std::lock_guard<std::mutex> lock(devices_mutex_);
        auto it = devices_.find(device_id);
        if (it != devices_.end()) {
            it->second.rtsp_urls = rtsp_urls;
            it->second.rtsp_urls_expire_time = now + std::chrono::seconds(RTSP_URL_CACHE_SECONDS);
        }
    }
    
    return rtsp_urls;
}

Result<void> ISAPIGateway::StartDeviceStream(const std::string& device_id,
                                    const std::string& channel_id,
                                    const std::string& target_app,
                                    const std::string& target_stream) {
    std::string stream_id = GenerateStreamId(target_app, target_stream);
    
    // 检查流是否已存在
    auto [exists, is_running] = GatewayBase::CheckStreamExists<StreamInfo>(
        stream_id, streams_, streams_mutex_, target_app, target_stream,
        [this](StreamInfo& info) { 
            std::string stream_id = GenerateStreamId(info.target_app, info.target_stream);
            if (info.pid > 0) {
                ffmpeg_helper_->StopProcess(info.pid, stream_id,
                    [&info](GatewayStatus status) { info.status = status; });
            } else {
                if (zlm_client_) {
                    zlm_client_->DeleteStream(info.target_app, info.target_stream);
                }
            }
        });
    
    if (exists && is_running) {
        return Result<void>::Success();
    }
    
    std::lock_guard<std::mutex> lock(streams_mutex_);  // CheckStreamExists已经释放锁，这里重新加锁
    
    // 获取 RTSP 地址
    std::vector<std::string> rtsp_urls = GetDeviceRTSPURLs(device_id, channel_id);
    if (rtsp_urls.empty()) {
        return Result<void>::Failure(gateway::DeviceConnectionException("Failed to get RTSP URL for device: " + device_id));
    }
    
    std::string rtsp_url = rtsp_urls[0];  // 使用第一个 RTSP 地址
    
    // 创建流信息
    StreamInfo info;
    info.device_id = device_id;
    info.channel_id = channel_id.empty() ? "1" : channel_id;
    info.rtsp_url = rtsp_url;
    info.target_app = target_app;
    info.target_stream = target_stream;
    info.status = GatewayStatus::Starting;
    
    // 获取设备认证信息（用于检测）
    std::string detect_username, detect_password;
    {
        std::lock_guard<std::mutex> device_lock(devices_mutex_);
        auto device_it = devices_.find(device_id);
        if (device_it != devices_.end()) {
            detect_username = device_it->second.username;
            detect_password = device_it->second.password;
        }
    }
    
    // 使用 SmartStreamProcessor 处理流启动
    auto result = smart_processor_->ProcessStream(
        rtsp_url, target_app, target_stream, "", "rtsp", "isapi_gateway",
        // 直接代理回调
        [this](const std::string& app, const std::string& stream, const std::string& url) {
            return zlm_client_ && zlm_client_->AddRTSPStream(app, stream, url);
        },
        // 获取流信息回调
        [this](const std::string& app, const std::string& stream, const std::string& schema) {
            return zlm_client_ ? zlm_client_->GetStreamInfo(app, stream, schema.empty() ? "rtsp" : schema) : streaming::StreamInfo();
        },
        // 启动 FFmpeg 回调
        [this, &info, stream_id](const gateway::utils::StreamInfoResult& stream_info_result) {
            // 获取智能分配的码率
            // 获取智能分配的码率（考虑源流码率，不降级）
            int source_bitrate_kbps = stream_info_result.total_bitrate > 0 ? stream_info_result.total_bitrate / 1000 : 0;
            int recommended_bitrate = bitrate_helper_->GetRecommendedBitrate(
                info.target_app, info.target_stream, "isapi", "",
                stream_info_result.video_width > 0 ? std::to_string(stream_info_result.video_width) + "x" + std::to_string(stream_info_result.video_height) : "1280x720",
                static_cast<int>(stream_info_result.video_fps > 0.0 ? stream_info_result.video_fps : 30),
                5,  // priority
                source_bitrate_kbps);
            
            std::string command = BuildFFmpegCommand(info, stream_info_result, recommended_bitrate);
            
            LOG_DEBUG("[ISAPI Gateway] FFmpeg 命令: {}", command);
            
            int pid = 0;
            GatewayStatus status = GatewayStatus::Stopped;
            
            std::string log_file = "/tmp/ffmpeg_isapi_" + info.target_app + "_" + info.target_stream + ".log";
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
        "rtsp",  // stream_schema
        20,      // detect_timeout
        detect_username,
        detect_password
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
        LOG_INFO("ISAPI Gateway 启动成功: 设备 {} -> {}/{}", device_id, target_app, target_stream);
        return Result<void>::Success();
    } else {
        // 错误已在 SmartStreamProcessor 中处理
        info.status = GatewayStatus::Error;
        streams_[stream_id] = info;
        return Result<void>::Failure(gateway::InternalServerException(result.error_message));
    }
}

Result<void> ISAPIGateway::StopDeviceStream(const std::string& target_app,
                                   const std::string& target_stream) {
    std::string stream_id = GenerateStreamId(target_app, target_stream);
    
    return GatewayBase::StopWithFFmpeg<StreamInfo>(
        stream_id, target_app, target_stream,
        streams_, streams_mutex_,
        [this](StreamInfo& info) { 
            std::string stream_id = GenerateStreamId(info.target_app, info.target_stream);
            if (info.pid > 0) {
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
        "ISAPI");
}

std::string ISAPIGateway::BuildFFmpegCommand(const StreamInfo& info, 
                                             const gateway::utils::StreamInfoResult& stream_info_result,
                                             int bitrate_kbps) {
    std::ostringstream oss;
    
    // FFmpeg 命令：从 RTSP 源拉流并推送到 ZLMediaKit
    oss << ffmpeg_path_;
    
    // 输入参数：RTSP 源流
    oss << " -rtsp_transport tcp"
        << " -i \"" << info.rtsp_url << "\"";
    
    // 智能转码决策：根据 stream_info_result 决定转码策略
    bool video_compatible = gateway::utils::StreamInfoDetector::IsVideoCodecCompatible(stream_info_result.video_codec);
    bool audio_compatible = gateway::utils::StreamInfoDetector::IsAudioCodecCompatible(stream_info_result.audio_codec);
    bool video_only_transcode = video_compatible && !audio_compatible;
    
    if (video_only_transcode) {
        // 只转码音频，视频直接复制
        oss << " -c:v copy";  // 视频直接复制，保持源流质量
        LOG_INFO("[ISAPIGateway] Video({}) is compatible, transcoding audio only, using -c:v copy", 
                stream_info_result.video_codec.empty() ? "(unknown)" : stream_info_result.video_codec);
        
        // 即使copy模式，也要控制传输速度（如果提供了码率）
        if (bitrate_kbps > 0) {
            ::utils::FFmpegParams::AddRateLimitParams(oss, bitrate_kbps);
        }
    } else {
        // 转码视频和音频（保持分辨率）
        int final_bitrate = (bitrate_kbps > 0) ? bitrate_kbps : 3500;
        oss << " -c:v libx264 -preset veryfast"
            << " -g 25";
        
        // 智能分辨率处理：保持源流分辨率，不强制缩放
        if (stream_info_result.video_width > 0 && stream_info_result.video_height > 0) {
            // 保持源流分辨率，不添加 -vf scale
            LOG_INFO("[ISAPIGateway] 保持源流分辨率 {}x{}，不强制缩放", 
                     stream_info_result.video_width, stream_info_result.video_height);
        } else {
            // 无法检测分辨率，使用默认缩放（保持向后兼容）
            oss << " -vf scale=1920:1080";
            LOG_INFO("[ISAPIGateway] 未检测到分辨率，使用默认缩放 1920x1080");
        }
        
        ::utils::FFmpegParams::AddVideoBitrateParams(oss, final_bitrate);
    }
    
    // 智能音频处理：如果源流音频已经是 AAC，使用 copy 保持质量
    if (stream_info_result.audio_codec == "aac") {
        oss << " -c:a copy";  // 如果源流已经是 AAC，直接复制，避免重新编码导致的质量损耗
        LOG_INFO("[ISAPIGateway] 源流音频是 AAC，使用 -c:a copy 保持质量");
    } else {
        oss << " -c:a aac -b:a 128k -ar 44100 -ac 2";  // 只有不是 AAC 时才转码
        LOG_INFO("[ISAPIGateway] 源流音频是 {}，转码为 AAC", stream_info_result.audio_codec.empty() ? "(unknown)" : stream_info_result.audio_codec);
    }
    
    // 输出参数 - 统一使用 RTMP/FLV 推流格式
    oss << " -f flv"                    // 输出格式为 FLV (推送到 ZLM)
        << " \"" << ::utils::zlm_url_builder::BuildRTMPUrlWithSecret(
            config_, info.target_app, info.target_stream, "ISAPI Gateway") << "\"";
    
    return oss.str();
}


std::vector<ISAPIDevice> ISAPIGateway::DiscoverDevicesHTTP(const std::string& ip_range,
                                                           int port,
                                                           int timeout_seconds) {
    std::vector<ISAPIDevice> devices;
    
    // 解析 IP 地址范围
    std::vector<std::string> ip_list = ParseIPRange(ip_range);
    
    // 如果 IP 列表为空，使用常见网段
    if (ip_list.empty()) {
        // 默认扫描 192.168.1.0/24
        for (int i = 1; i <= 254; i++) {
            ip_list.push_back("192.168.1." + std::to_string(i));
        }
    }
    
    // 顺序探测设备
    for (const auto& ip : ip_list) {
        ISAPIDevice device = ProbeDevice(ip, port, timeout_seconds);
        if (!device.id.empty()) {
            devices.push_back(device);
        }
    }
    
    return devices;
}

ISAPIDevice ISAPIGateway::ProbeDevice(const std::string& ip, int port, int /* timeout_seconds */) {
    std::string base_url = "http://" + ip + ":" + std::to_string(port);
    return GetDeviceInfo(base_url);
}

ISAPIDevice ISAPIGateway::GetDeviceInfo(const std::string& base_url,
                                        const std::string& username,
                                        const std::string& password) {
    // ISAPI 设备信息 API: /ISAPI/System/deviceInfo
    std::string url = base_url + "/ISAPI/System/deviceInfo";
    std::string response = SendISAPIRequest(url, "GET", "", username, password);
    
    if (response.empty()) {
        return ISAPIDevice();
    }
    
    ISAPIDevice device;
    device.base_url = base_url;
    device.username = username;
    device.password = password;
    
    // 解析 XML 响应
    // 示例响应格式：
    // <DeviceInfo>
    //   <deviceName>Hikvision</deviceName>
    //   <deviceID>...</deviceID>
    //   <model>...</model>
    //   <serialNumber>...</serialNumber>
    //   <firmwareVersion>...</firmwareVersion>
    // </DeviceInfo>
    
    std::regex name_regex(R"(<deviceName[^>]*>([^<]+)</deviceName>)");
    std::regex model_regex(R"(<model[^>]*>([^<]+)</model>)");
    std::regex serial_regex(R"(<serialNumber[^>]*>([^<]+)</serialNumber>)");
    std::regex version_regex(R"(<firmwareVersion[^>]*>([^<]+)</firmwareVersion>)");
    
    std::smatch match;
    if (std::regex_search(response, match, name_regex)) {
        device.manufacturer = match[1].str();
    }
    if (std::regex_search(response, match, model_regex)) {
        device.model = match[1].str();
    }
    if (std::regex_search(response, match, serial_regex)) {
        device.serial_number = match[1].str();
    }
    if (std::regex_search(response, match, version_regex)) {
        device.firmware_version = match[1].str();
    }
    
    // 如果解析到设备信息，认为是有效的 ISAPI 设备
    if (!device.model.empty() || !device.serial_number.empty()) {
        device.id = GenerateDeviceID(base_url);
        device.manufacturer = device.manufacturer.empty() ? "Hikvision" : device.manufacturer;
        return device;
    }
    
    return ISAPIDevice();
}

std::vector<std::string> ISAPIGateway::GetChannels(const ISAPIDevice& device) {
    // ISAPI 通道列表 API: /ISAPI/Streaming/channels
    std::string url = device.base_url + "/ISAPI/Streaming/channels";
    std::string response = SendISAPIRequest(url, "GET", "", device.username, device.password);
    
    if (response.empty()) {
        return {};
    }
    
    std::vector<std::string> channels;
    
    // 解析 XML 响应，提取通道 ID
    // 示例: <StreamingChannel id="1">...</StreamingChannel>
    std::regex channel_regex(R"(<StreamingChannel[^>]*id=\"(\d+)\"[^>]*>)");
    std::sregex_iterator iter(response.begin(), response.end(), channel_regex);
    std::sregex_iterator end;
    
    for (; iter != end; ++iter) {
        channels.push_back(iter->str(1));
    }
    
    // 如果没有找到通道，默认返回通道 1
    if (channels.empty()) {
        channels.push_back("1");
    }
    
    return channels;
}

std::string ISAPIGateway::GetStreamURI(const ISAPIDevice& device, const std::string& channel_id) {
    // ISAPI 流地址 API: /ISAPI/Streaming/channels/{channel_id}/url
    std::string url = device.base_url + "/ISAPI/Streaming/channels/" + channel_id + "/url";
    std::string response = SendISAPIRequest(url, "GET", "", device.username, device.password);
    
    if (response.empty()) {
        return "";
    }
    
    // 解析 XML 响应，提取 RTSP URL
    // 示例: <url>rtsp://192.168.1.100:554/Streaming/Channels/101</url>
    std::regex url_regex(R"(<url[^>]*>([^<]+)</url>)");
    std::smatch match;
    
    if (std::regex_search(response, match, url_regex)) {
        return match[1].str();
    }
    
    // 如果解析失败，尝试构建默认 RTSP URL
    // 格式: rtsp://{ip}:554/Streaming/Channels/{channel_id}01
    size_t pos = device.base_url.find("://");
    if (pos != std::string::npos) {
        std::string ip_port = device.base_url.substr(pos + 3);
        size_t colon_pos = ip_port.find(':');
        std::string ip = (colon_pos != std::string::npos) ? ip_port.substr(0, colon_pos) : ip_port;
        
        std::string rtsp_url = "rtsp://" + ip + ":554/Streaming/Channels/" + channel_id + "01";
        if (!device.username.empty()) {
            rtsp_url = "rtsp://" + device.username + ":" + device.password + "@" + ip + ":554/Streaming/Channels/" + channel_id + "01";
        }
        return rtsp_url;
    }
    
    return "";
}

std::string ISAPIGateway::SendISAPIRequest(const std::string& url,
                                          const std::string& method,
                                          const std::string& body,
                                          const std::string& username,
                                          const std::string& password) {
    CURL* curl = curl_easy_init();
    if (!curl) {
        LOG_ERROR("初始化 CURL 失败");
        return "";
    }
    
    std::string response_data;
    response_data.reserve(8192);
    
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, ::utils::http_callback::WriteCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response_data);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 10L);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 5L);
    
    if (!username.empty() && !password.empty()) {
        curl_easy_setopt(curl, CURLOPT_HTTPAUTH, CURLAUTH_DIGEST | CURLAUTH_BASIC);
        curl_easy_setopt(curl, CURLOPT_USERNAME, username.c_str());
        curl_easy_setopt(curl, CURLOPT_PASSWORD, password.c_str());
    }
    
    if (method == "POST" || method == "PUT") {
        curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body.c_str());
        curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, body.length());
        if (method == "PUT") {
            curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, "PUT");
        }
    } else if (method == "DELETE") {
        curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, "DELETE");
    }
    
    struct curl_slist* headers = nullptr;
    headers = curl_slist_append(headers, "Content-Type: application/xml");
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    
    try {
        CURLcode res = curl_easy_perform(curl);
        
        long response_code = 0;
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &response_code);
        
        curl_slist_free_all(headers);
        curl_easy_cleanup(curl);
        
        if (res != CURLE_OK) {
            LOG_ERROR("ISAPI 请求失败: {} (CURL error: {})", url, curl_easy_strerror(res));
            return "";
        }
        
        if (response_code != 200) {
            LOG_ERROR("ISAPI 请求返回错误状态码: {} (HTTP {})", url, response_code);
            return "";
        }
        
        LOG_DEBUG("ISAPI 请求成功: {} (HTTP {}), 响应长度: {} 字节", url, response_code, response_data.length());
        return response_data;
    } catch (const std::exception& e) {
        LOG_ERROR("ISAPI 请求异常: {} (异常信息: {})", url, e.what());
        curl_slist_free_all(headers);
        curl_easy_cleanup(curl);
        return "";
    } catch (...) {
        LOG_ERROR("ISAPI 请求未知异常: {}", url);
        curl_slist_free_all(headers);
        curl_easy_cleanup(curl);
        return "";
    }
}

std::string ISAPIGateway::GenerateDeviceID(const std::string& base_url) const {
    std::hash<std::string> hasher;
    size_t hash = hasher(base_url);
    std::ostringstream oss;
    oss << "isapi_device_" << std::hex << hash;
    return oss.str();
}

std::vector<std::string> ISAPIGateway::ParseIPRange(const std::string& ip_range) const {
    std::vector<std::string> ip_list;
    
    if (ip_range.empty()) {
        return ip_list;
    }
    
    // 简单实现：支持 CIDR 格式（如 192.168.1.0/24）
    size_t slash_pos = ip_range.find('/');
    if (slash_pos == std::string::npos) {
        // 单个 IP
        ip_list.push_back(ip_range);
        return ip_list;
    }
    
    std::string base_ip = ip_range.substr(0, slash_pos);
    int prefix_len = std::stoi(ip_range.substr(slash_pos + 1));
    
    // 解析基础 IP
    std::vector<int> parts;
    std::istringstream iss(base_ip);
    std::string part;
    while (std::getline(iss, part, '.')) {
        parts.push_back(std::stoi(part));
    }
    
    if (parts.size() != 4) {
        return ip_list;
    }
    
    // 计算网络地址和主机范围
    int host_bits = 32 - prefix_len;
    
    // 特殊处理 /32
    if (host_bits == 0) {
        ip_list.push_back(base_ip);
        return ip_list;
    }
    
    int host_count = (1 << host_bits) - 2;  // 排除网络地址和广播地址
    
    if (host_count > 1000) {
        // 限制扫描范围，避免扫描过多 IP
        LOG_WARN("IP 范围过大，限制扫描前 1000 个地址");
        host_count = 1000;
    }
    
    // 生成 IP 列表
    for (int i = 1; i <= host_count; i++) {
        int ip = (parts[0] << 24) | (parts[1] << 16) | (parts[2] << 8) | parts[3];
        ip += i;
        
        int a = (ip >> 24) & 0xFF;
        int b = (ip >> 16) & 0xFF;
        int c = (ip >> 8) & 0xFF;
        int d = ip & 0xFF;
        
        ip_list.push_back(std::to_string(a) + "." + std::to_string(b) + "." + 
                         std::to_string(c) + "." + std::to_string(d));
    }
    
    return ip_list;
}

} // namespace gateway

