#include "gateway/onvif/onvif_gateway.hpp"
#include "config/config_loader.hpp"
#include "streaming/zlmediakit/zlm_client.hpp"
#include "streaming/stream_manager.hpp"
#include "utils/logger.hpp"
#include "utils/http_callback.hpp"
#include "utils/url_parser.hpp"
#include "gateway/utils/gateway_config_helper.hpp"
#include "gateway/utils/smart_stream_processor.hpp"
#include "gateway/utils/ffmpeg_process_helper.hpp"
#include "gateway/utils/bitrate_allocation_helper.hpp"
#include "process/process_manager.hpp"
#include "process/ffmpeg_executor.hpp"
#include "utils/zlm_url_builder.hpp"
#include "utils/ffmpeg_params.hpp"
#include "utils/stream_status_checker.hpp"
#include "utils/bitrate_allocator.hpp"
#include <sstream>
#include <random>
#include <algorithm>
#include <cstring>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/select.h>
#include <curl/curl.h>
#include <regex>
#include <thread>
#include <chrono>


#include "gateway/utils/gateway_registry.hpp"

namespace gateway {

namespace {
static gateway::utils::AutoRegister reg("onvif", [](const gateway::utils::GatewayContext& ctx) -> std::shared_ptr<gateway::GatewayBase> {
    if (ctx.config->onvif.enabled) {
        return std::make_shared<ONVIFGateway>(ctx.config, ctx.zlm_client, ctx.process_manager, ctx.stream_manager);
    }
    return nullptr;
});
}


ONVIFGateway::ONVIFGateway(std::shared_ptr<config::Config> config,
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
    
    LOG_INFO("ONVIF Gateway 初始化完成（支持直接代理和转码）");
}

ONVIFGateway::~ONVIFGateway() {
    // 停止所有流
    GatewayBase::StopAllStreamsWithFFmpeg<StreamInfo>(
        streams_, streams_mutex_,
        [this](StreamInfo& info) { 
            std::string stream_id = GenerateStreamId(info.target_app, info.target_stream);
            ffmpeg_helper_->StopProcess(info.pid, stream_id,
                [&info](GatewayStatus status) { info.status = status; });
        });
}

Result<void> ONVIFGateway::Start(const std::string& source_url,
                        const std::string& target_app,
                        const std::string& target_stream,
                        const std::string& /* output_protocol */) {
    // source_url 格式: onvif://device_id[/profile_token]
    // 例如: onvif://device_123 或 onvif://device_123/profile_0
    
    if (!::utils::url_parser::ValidateURL(source_url, "onvif://")) {
        return Result<void>::Failure(gateway::InvalidParameterException("Invalid source URL format: " + source_url));
    }
    
    auto [device_id, profile_token] = ::utils::url_parser::ParseDeviceURL(source_url, "onvif://");
    
    return StartDeviceStream(device_id, profile_token, target_app, target_stream);
}

Result<void> ONVIFGateway::Stop(const std::string& target_app,
                       const std::string& target_stream) {
    return StopDeviceStream(target_app, target_stream);
}

bool ONVIFGateway::IsRunning(const std::string& target_app,
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

GatewayStatus ONVIFGateway::GetStatus(const std::string& target_app,
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

std::vector<ONVIFDevice> ONVIFGateway::DiscoverDevices(int timeout_seconds) {
    LOG_INFO("开始发现 ONVIF 设备，超时时间: {} 秒", timeout_seconds);
    
    auto devices = DiscoverDevicesWSDiscovery(timeout_seconds);
    
    // 更新设备列表
    std::lock_guard<std::mutex> lock(devices_mutex_);
    auto now = std::chrono::system_clock::now();
    for (auto& device : devices) {
        device.last_seen = now;
        devices_[device.id] = device;
    }
    
    LOG_INFO("发现 {} 个 ONVIF 设备，已保存到设备列表", devices.size());
    return devices;
}

std::vector<ONVIFDevice> ONVIFGateway::ListDevices() const {
    std::lock_guard<std::mutex> lock(devices_mutex_);
    
    std::vector<ONVIFDevice> result;
    result.reserve(devices_.size());
    for (const auto& pair : devices_) {
        result.push_back(pair.second);
    }
    
    return result;
}

ONVIFDevice ONVIFGateway::GetDevice(const std::string& device_id) const {
    std::lock_guard<std::mutex> lock(devices_mutex_);
    
    auto it = devices_.find(device_id);
    if (it == devices_.end()) {
        return ONVIFDevice();  // 返回空对象
    }
    
    return it->second;
}

std::string ONVIFGateway::AddDevice(const ONVIFDevice& device) {
    std::lock_guard<std::mutex> lock(devices_mutex_);
    
    // 如果设备没有 ID，生成一个
    std::string device_id = device.id;
    if (device_id.empty()) {
        device_id = GenerateDeviceID(device.xaddr);
    }
    
    // 创建设备副本并设置时间戳
    ONVIFDevice device_copy = device;
    device_copy.id = device_id;
    device_copy.last_seen = std::chrono::system_clock::now();
    
    // 添加到设备列表
    devices_[device_id] = device_copy;
    
    LOG_INFO("手动添加设备: {} ({})", device_id, device.xaddr);
    return device_id;
}

bool ONVIFGateway::RemoveDevice(const std::string& device_id) {
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

bool ONVIFGateway::UpdateDeviceCredentials(const std::string& device_id,
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
    // 清除缓存的 RTSP 地址，强制重新获取
    it->second.rtsp_urls.clear();
    
    LOG_INFO("更新设备认证信息: {}", device_id);
    return true;
}

std::vector<std::string> ONVIFGateway::GetDeviceRTSPURLs(const std::string& device_id,
                                                         const std::string& profile_token) {
    // 获取设备信息
    ONVIFDevice device = GetDevice(device_id);
    if (device.id.empty()) {
        LOG_ERROR("设备不存在: {}", device_id);
        return {};
    }
    
    // 检查缓存的 RTSP 地址是否有效
    auto now = std::chrono::system_clock::now();
    if (!device.rtsp_urls.empty() && now < device.rtsp_urls_expire_time) {
        return device.rtsp_urls;
    }
    
    // 获取配置列表
    std::vector<std::string> profile_tokens = GetProfiles(device);
    if (profile_tokens.empty()) {
        LOG_ERROR("无法获取设备配置: {}", device_id);
        return {};
    }
    
    // 获取 RTSP 地址
    std::vector<std::string> rtsp_urls;
    if (profile_token.empty()) {
        // 获取所有配置的 RTSP 地址
        for (const auto& token : profile_tokens) {
            std::string url = GetStreamUri(device, token);
            if (!url.empty()) {
                rtsp_urls.push_back(url);
            }
        }
    } else {
        // 获取指定配置的 RTSP 地址
        std::string url = GetStreamUri(device, profile_token);
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

Result<void> ONVIFGateway::StartDeviceStream(const std::string& device_id,
                                    const std::string& profile_token,
                                    const std::string& target_app,
                                    const std::string& target_stream) {
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
    
    // 获取 RTSP 地址
    std::vector<std::string> rtsp_urls = GetDeviceRTSPURLs(device_id, profile_token);
    if (rtsp_urls.empty()) {
        return Result<void>::Failure(gateway::DeviceConnectionException("Failed to get RTSP URL for device: " + device_id));
    }
    
    std::string rtsp_url = rtsp_urls[0];  // 使用第一个 RTSP 地址
    
    // 创建流信息
    StreamInfo info;
    info.device_id = device_id;
    info.profile_token = profile_token.empty() ? (rtsp_urls.size() > 0 ? "default" : "") : profile_token;
    info.rtsp_url = rtsp_url;
    info.target_app = target_app;
    info.target_stream = target_stream;
    info.status = GatewayStatus::Starting;
    
    // 使用 SmartStreamProcessor 处理流启动
    auto result = smart_processor_->ProcessStream(
        rtsp_url, target_app, target_stream, "", "rtsp", "onvif_gateway",
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
                info.target_app, info.target_stream, "onvif", "",
                stream_info_result.video_width > 0 ? std::to_string(stream_info_result.video_width) + "x" + std::to_string(stream_info_result.video_height) : "1280x720",
                static_cast<int>(stream_info_result.video_fps > 0.0 ? stream_info_result.video_fps : 30),
                5,  // priority
                source_bitrate_kbps);
            
            std::string command = BuildFFmpegCommand(info, stream_info_result, recommended_bitrate);
            
            LOG_DEBUG("[ONVIF Gateway] FFmpeg 命令: {}", command);
            
            int pid = 0;
            GatewayStatus status = GatewayStatus::Stopped;
            
            std::string log_file = "/tmp/ffmpeg_onvif_" + info.target_app + "_" + info.target_stream + ".log";
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
        "rtsp"  // stream_schema
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
        LOG_INFO("ONVIF Gateway 启动成功: 设备 {} -> {}/{}", device_id, target_app, target_stream);
        return Result<void>::Success();
    } else {
        // 错误已在 SmartStreamProcessor 中处理
        info.status = GatewayStatus::Error;
        streams_[stream_id] = info;
        return Result<void>::Failure(gateway::InternalServerException(result.error_message));
    }
}

Result<void> ONVIFGateway::StopDeviceStream(const std::string& target_app,
                                   const std::string& target_stream) {
    std::string stream_id = GenerateStreamId(target_app, target_stream);
    
    return GatewayBase::StopWithFFmpeg<StreamInfo>(
        stream_id, target_app, target_stream,
        streams_, streams_mutex_,
        [this](StreamInfo& info) { 
            std::string stream_id = GenerateStreamId(info.target_app, info.target_stream);
            return ffmpeg_helper_->StopProcess(info.pid, stream_id,
                [&info](GatewayStatus status) { info.status = status; });
        },
        [this](const std::string& app, const std::string& stream) {
            if (zlm_client_) {
                zlm_client_->DeleteStream(app, stream);
            }
        },
        "ONVIF");
}

std::string ONVIFGateway::BuildFFmpegCommand(const StreamInfo& info, 
                                               const gateway::utils::StreamInfoResult& stream_info_result,
                                               int bitrate_kbps) {
    std::ostringstream oss;
    
    // FFmpeg 命令：从 RTSP 源拉流并推送到 ZLMediaKit
    oss << ffmpeg_path_;
    
    // 输入参数：RTSP 源流
    oss << " -rtsp_transport tcp"
        << " -i \"" << info.rtsp_url << "\"";
    
    // 智能转码决策：根据 stream_info_result 决定转码策略
    // 检查视频和音频兼容性
    bool video_compatible = gateway::utils::StreamInfoDetector::IsVideoCodecCompatible(stream_info_result.video_codec);
    bool audio_compatible = gateway::utils::StreamInfoDetector::IsAudioCodecCompatible(stream_info_result.audio_codec);
    bool video_only_transcode = video_compatible && !audio_compatible;
    
    if (video_only_transcode) {
        // 只转码音频，视频直接复制
        oss << " -c:v copy";  // 视频直接复制，保持源流质量
        LOG_INFO("[ONVIFGateway] Video({}) is compatible, transcoding audio only, using -c:v copy", 
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
            LOG_INFO("[ONVIFGateway] 保持源流分辨率 {}x{}，不强制缩放", 
                     stream_info_result.video_width, stream_info_result.video_height);
        } else {
            // 无法检测分辨率，使用默认缩放（保持向后兼容）
            oss << " -vf scale=1920:1080";
            LOG_INFO("[ONVIFGateway] 未检测到分辨率，使用默认缩放 1920x1080");
        }
        
        ::utils::FFmpegParams::AddVideoBitrateParams(oss, final_bitrate);
    }
    
    // 智能音频处理：如果源流音频已经是 AAC，使用 copy 保持质量
    if (stream_info_result.audio_codec == "aac") {
        oss << " -c:a copy";  // 如果源流已经是 AAC，直接复制，避免重新编码导致的质量损耗
        LOG_INFO("[ONVIFGateway] 源流音频是 AAC，使用 -c:a copy 保持质量");
    } else {
        oss << " -c:a aac -b:a 128k -ar 44100 -ac 2";  // 只有不是 AAC 时才转码
        LOG_INFO("[ONVIFGateway] 源流音频是 {}，转码为 AAC", stream_info_result.audio_codec.empty() ? "(unknown)" : stream_info_result.audio_codec);
    }
    
    // 输出参数 - 统一使用 RTMP/FLV 推流格式
    oss << " -f flv"                    // 输出格式为 FLV (推送到 ZLM)
        << " \"" << ::utils::zlm_url_builder::BuildRTMPUrlWithSecret(
            config_, info.target_app, info.target_stream, "ONVIF Gateway") << "\"";
    
    return oss.str();
}

std::vector<ONVIFDevice> ONVIFGateway::DiscoverDevicesWSDiscovery(int timeout_seconds) {
    return SendWSDiscoveryProbe(timeout_seconds);
}

std::vector<ONVIFDevice> ONVIFGateway::SendWSDiscoveryProbe(int timeout_seconds) {
    // WS-Discovery 使用 UDP 多播，地址为 239.255.255.250:3702
    // 在 macOS 上，如果多播失败，尝试使用单播回退到本地模拟器
    const char* multicast_addr = "239.255.255.250";
    const int multicast_port = 3702;
    const char* fallback_addr = "127.0.0.1";  // macOS 回退地址
    
    // 创建 UDP socket
    int sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock < 0) {
        LOG_ERROR("创建 UDP socket 失败: {}", strerror(errno));
        return {};
    }
    
    // 设置 socket 选项：允许地址重用
    int reuse = 1;
    if (setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse)) < 0) {
        LOG_ERROR("设置 SO_REUSEADDR 失败: {}", strerror(errno));
        close(sock);
        return {};
    }
    
    // 绑定到本地端口
    struct sockaddr_in local_addr;
    memset(&local_addr, 0, sizeof(local_addr));
    local_addr.sin_family = AF_INET;
    local_addr.sin_addr.s_addr = INADDR_ANY;
    local_addr.sin_port = 0;  // 系统自动分配端口
    
    if (bind(sock, (struct sockaddr*)&local_addr, sizeof(local_addr)) < 0) {
        LOG_ERROR("绑定 UDP socket 失败: {}", strerror(errno));
        close(sock);
        return {};
    }
    
    // 构建 Probe 消息
    std::string message_id = GenerateUUID();
    std::ostringstream probe_msg;
    probe_msg << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
              << "<s:Envelope xmlns:s=\"http://www.w3.org/2003/05/soap-envelope\" "
              << "xmlns:a=\"http://schemas.xmlsoap.org/ws/2004/08/addressing\" "
              << "xmlns:d=\"http://schemas.xmlsoap.org/ws/2005/04/discovery\">\n"
              << "<s:Header>\n"
              << "<a:Action s:mustUnderstand=\"1\">http://schemas.xmlsoap.org/ws/2005/04/discovery/Probe</a:Action>\n"
              << "<a:MessageID>uuid:" << message_id << "</a:MessageID>\n"
              << "<a:To s:mustUnderstand=\"1\">urn:schemas-xmlsoap-org:ws:2005:04:discovery</a:To>\n"
              << "</s:Header>\n"
              << "<s:Body>\n"
              << "<d:Probe>\n"
              << "<d:Types>dn:NetworkVideoTransmitter</d:Types>\n"
              << "</d:Probe>\n"
              << "</s:Body>\n"
              << "</s:Envelope>";
    
    std::string probe_xml = probe_msg.str();
    
    // 设置多播地址
    struct sockaddr_in multicast_sockaddr;
    memset(&multicast_sockaddr, 0, sizeof(multicast_sockaddr));
    multicast_sockaddr.sin_family = AF_INET;
    multicast_sockaddr.sin_addr.s_addr = inet_addr(multicast_addr);
    multicast_sockaddr.sin_port = htons(multicast_port);
    
    // 发送 Probe 消息
    ssize_t sent = sendto(sock, probe_xml.c_str(), probe_xml.length(), 0,
                         (struct sockaddr*)&multicast_sockaddr, sizeof(multicast_sockaddr));
    if (sent < 0) {
        LOG_ERROR("发送 Probe 消息失败: {}", strerror(errno));
        close(sock);
        return {};
    }
    
    
    // 接收响应
    std::vector<ONVIFDevice> devices;
    std::map<std::string, bool> device_map;  // 用于去重（基于 XAddr）
    
    char buffer[4096];
    struct sockaddr_in from_addr;
    socklen_t from_len = sizeof(from_addr);
    
    auto start_time = std::chrono::steady_clock::now();
    auto timeout = std::chrono::seconds(timeout_seconds);
    
    // 设置 socket 为非阻塞模式，以便可以超时
    int flags = fcntl(sock, F_GETFL, 0);
    fcntl(sock, F_SETFL, flags | O_NONBLOCK);
    auto response_timeout = std::chrono::milliseconds(500);  // 收到响应后的等待时间
    
    while (true) {
        auto elapsed = std::chrono::steady_clock::now() - start_time;
        if (elapsed >= timeout) {
            break;
        }
        
        // 设置接收超时
        auto remaining = timeout - elapsed;
        auto remaining_ms = std::chrono::duration_cast<std::chrono::milliseconds>(remaining);
        auto tv_timeout = std::chrono::duration_cast<std::chrono::microseconds>(
            std::min(remaining_ms, response_timeout));
        
        struct timeval tv;
        tv.tv_sec = tv_timeout.count() / 1000000;
        tv.tv_usec = tv_timeout.count() % 1000000;
        
        fd_set read_fds;
        FD_ZERO(&read_fds);
        FD_SET(sock, &read_fds);
        
        int select_result = select(sock + 1, &read_fds, nullptr, nullptr, &tv);
        if (select_result <= 0) {
            // 超时或错误
            if (select_result == 0 && !devices.empty()) {
                // 已收到响应且超时，退出
                break;
            }
            continue;
        }
        
        // 接收数据
        ssize_t received = recvfrom(sock, buffer, sizeof(buffer) - 1, 0,
                                   (struct sockaddr*)&from_addr, &from_len);
        if (received < 0) {
            continue;
        }
        
        buffer[received] = '\0';
        std::string response_xml(buffer, received);
        
        // 解析响应
        ONVIFDevice device = ParseProbeMatchResponse(response_xml);
        if (!device.id.empty() && device_map.find(device.xaddr) == device_map.end()) {
            device_map[device.xaddr] = true;
            devices.push_back(device);
        }
    }
    
    close(sock);
    
    // 如果在 macOS 上多播失败，尝试单播回退到本地模拟器
    #ifdef __APPLE__
    if (devices.empty()) {
        LOG_DEBUG("多播未发现设备，尝试单播回退到本地模拟器: {}", fallback_addr);
        
        // 创建新的 socket 用于单播
        int unicast_sock = socket(AF_INET, SOCK_DGRAM, 0);
        if (unicast_sock >= 0) {
            // 绑定到本地端口以接收响应
            struct sockaddr_in local_unicast;
            memset(&local_unicast, 0, sizeof(local_unicast));
            local_unicast.sin_family = AF_INET;
            local_unicast.sin_addr.s_addr = INADDR_ANY;
            local_unicast.sin_port = 0;  // 系统自动分配端口
            
            if (bind(unicast_sock, (struct sockaddr*)&local_unicast, sizeof(local_unicast)) >= 0) {
                // 设置单播目标地址
                struct sockaddr_in unicast_sockaddr;
                memset(&unicast_sockaddr, 0, sizeof(unicast_sockaddr));
                unicast_sockaddr.sin_family = AF_INET;
                unicast_sockaddr.sin_addr.s_addr = inet_addr(fallback_addr);
                unicast_sockaddr.sin_port = htons(multicast_port);
                
                // 发送 Probe 消息到本地模拟器
                ssize_t sent = sendto(unicast_sock, probe_xml.c_str(), probe_xml.length(), 0,
                                     (struct sockaddr*)&unicast_sockaddr, sizeof(unicast_sockaddr));
                if (sent >= 0) {
                    LOG_DEBUG("已发送单播 Probe 消息到 {}:{}", fallback_addr, multicast_port);
                    
                    // 等待响应
                    struct timeval tv;
                    tv.tv_sec = 2;  // 2 秒超时
                    tv.tv_usec = 0;
                    
                    fd_set read_fds;
                    FD_ZERO(&read_fds);
                    FD_SET(unicast_sock, &read_fds);
                    
                    int select_result = select(unicast_sock + 1, &read_fds, nullptr, nullptr, &tv);
                    if (select_result > 0 && FD_ISSET(unicast_sock, &read_fds)) {
                        ssize_t received = recvfrom(unicast_sock, buffer, sizeof(buffer) - 1, 0,
                                                   (struct sockaddr*)&from_addr, &from_len);
                        if (received > 0) {
                            buffer[received] = '\0';
                            std::string response_xml(buffer, received);
                            
                            // 解析响应
                            ONVIFDevice device = ParseProbeMatchResponse(response_xml);
                            if (!device.id.empty()) {
                                devices.push_back(device);
                                LOG_INFO("通过单播回退发现设备: {} ({})", device.id, device.xaddr);
                            } else {
                                LOG_WARN("单播响应解析失败");
                                LOG_WARN("响应前500字符: {}", response_xml.substr(0, 500));
                                // 尝试直接搜索 XAddrs
                                size_t xaddrs_pos = response_xml.find("XAddrs");
                                if (xaddrs_pos != std::string::npos) {
                                    LOG_WARN("找到 XAddrs 标签，位置: {}", xaddrs_pos);
                                    LOG_WARN("XAddrs 周围内容: {}", response_xml.substr(std::max(0, (int)xaddrs_pos - 50), 200));
                                }
                            }
                        }
                    } else {
                    }
                } else {
                }
            } else {
            }
            close(unicast_sock);
        }
    }
    #endif
    
    return devices;
}

ONVIFDevice ONVIFGateway::ParseProbeMatchResponse(const std::string& response_xml) {
    ONVIFDevice device;
    
    // 简单的 XML 解析（使用正则表达式）
    // 实际项目中应该使用 XML 解析库（如 tinyxml2 或 pugixml）
    
    // 提取 XAddr - 尝试多种可能的格式
    // 注意：模拟器使用 <d:XAddrs> 标签（注意是复数 XAddrs，不是 XAddr）
    // Python ElementTree 可能使用不同的命名空间前缀（如 ns0, ns1, ns2 等）
    // 实际格式: <ns2:XAddrs>http://127.0.0.1:8081/onvif/device_service</ns2:XAddrs>
    std::vector<std::regex> xaddr_regexes = {
        std::regex(R"(<[^:]*:XAddrs[^>]*>([^<]+)</[^:]*:XAddrs>)"),  // 任意命名空间前缀（最通用）
        std::regex(R"(<d:XAddrs[^>]*>([^<]+)</d:XAddrs>)"),          // 标准格式 d:
        std::regex(R"(<ns2:XAddrs[^>]*>([^<]+)</ns2:XAddrs>)"),      // 模拟器格式 ns2:
        std::regex(R"(XAddrs[^>]*>([^<]+)</[^:]*XAddrs>)"),          // 更宽松的匹配
        std::regex(R"(XAddrs[^>]*>([^<]+)</XAddrs>)"),               // 最宽松的匹配
        std::regex(R"(<a:XAddr[^>]*>([^<]+)</a:XAddr>)")            // 单数形式（某些设备）
    };
    
    for (size_t i = 0; i < xaddr_regexes.size(); ++i) {
        std::smatch xaddr_match;
        if (std::regex_search(response_xml, xaddr_match, xaddr_regexes[i])) {
            device.xaddr = xaddr_match[1].str();
            LOG_INFO("使用正则表达式模式 {} 成功提取 XAddr: {}", i, device.xaddr);
            break;
        }
    }
    
    if (device.xaddr.empty()) {
        LOG_WARN("所有 XAddr 正则表达式都未匹配，响应长度: {}", response_xml.length());
        // 尝试简单的字符串搜索作为最后手段
        // 查找 <...:XAddrs> 或 XAddrs> 标签
        size_t xaddrs_start = response_xml.find("XAddrs>");
        if (xaddrs_start != std::string::npos) {
            // 跳过 "XAddrs>" 这7个字符
            size_t content_start = xaddrs_start + 7;
            size_t xaddrs_end = response_xml.find("</", content_start);
            if (xaddrs_end != std::string::npos) {
                device.xaddr = response_xml.substr(content_start, xaddrs_end - content_start);
                LOG_INFO("使用字符串搜索提取 XAddr: {}", device.xaddr);
            } else {
                LOG_WARN("找到 XAddrs> 但未找到结束标签");
            }
        } else {
            LOG_WARN("未找到 XAddrs 标签");
        }
    }
    
    if (device.xaddr.empty()) {
        LOG_WARN("ParseProbeMatchResponse: XAddr 为空，返回空设备对象");
        return device;  // 返回空对象
    }
    
    // 生成设备 ID
    device.id = GenerateDeviceID(device.xaddr);
    LOG_INFO("ParseProbeMatchResponse: 生成设备 ID: {} (XAddr: {})", device.id, device.xaddr);
    
    // 提取 Types - 尝试多种格式
    // 实际格式: <ns1:Types>dn:NetworkVideoTransmitter</ns1:Types>
    std::vector<std::regex> types_regexes = {
        std::regex(R"(<[^:]*:Types[^>]*>([^<]+)</[^:]*:Types>)"),  // 任意命名空间前缀（最通用）
        std::regex(R"(<a:Types[^>]*>([^<]+)</a:Types>)"),          // 标准格式 a:
        std::regex(R"(<ns1:Types[^>]*>([^<]+)</ns1:Types>)"),      // 模拟器格式 ns1:
        std::regex(R"(Types[^>]*>([^<]+)</Types>)")                // 最宽松的匹配
    };
    
    for (const auto& types_regex : types_regexes) {
        std::smatch types_match;
        if (std::regex_search(response_xml, types_match, types_regex)) {
            std::string types_str = types_match[1].str();
            // 简单的分割（实际应该更完善）
            size_t pos = 0;
            while ((pos = types_str.find(' ')) != std::string::npos) {
                device.types.push_back(types_str.substr(0, pos));
                types_str.erase(0, pos + 1);
            }
            if (!types_str.empty()) {
                device.types.push_back(types_str);
            }
            break;
        }
    }
    
    // 提取 Scopes - 尝试多种格式
    // 实际格式: <ns1:Scopes>onvif://www.onvif.org/name/TestCamera ...</ns1:Scopes>
    std::vector<std::regex> scopes_regexes = {
        std::regex(R"(<[^:]*:Scopes[^>]*>([^<]+)</[^:]*:Scopes>)"),  // 任意命名空间前缀（最通用）
        std::regex(R"(<a:Scopes[^>]*>([^<]+)</a:Scopes>)"),          // 标准格式 a:
        std::regex(R"(<ns1:Scopes[^>]*>([^<]+)</ns1:Scopes>)"),      // 模拟器格式 ns1:
        std::regex(R"(Scopes[^>]*>([^<]+)</Scopes>)")                // 最宽松的匹配
    };
    
    for (const auto& scopes_regex : scopes_regexes) {
        std::smatch scopes_match;
        if (std::regex_search(response_xml, scopes_match, scopes_regex)) {
            std::string scopes_str = scopes_match[1].str();
            // 简单的分割（实际应该更完善）
            size_t pos = 0;
            while ((pos = scopes_str.find(' ')) != std::string::npos) {
                device.scopes.push_back(scopes_str.substr(0, pos));
                scopes_str.erase(0, pos + 1);
            }
            if (!scopes_str.empty()) {
                device.scopes.push_back(scopes_str);
            }
            break;
        }
    }
    
    // 从 Scopes 中提取制造商、型号等信息
    for (const auto& scope : device.scopes) {
        if (scope.find("onvif://www.onvif.org/name/") == 0) {
            device.model = scope.substr(28);  // 移除前缀
        } else if (scope.find("onvif://www.onvif.org/hardware/") == 0) {
            device.hardware_id = scope.substr(32);  // 移除前缀
        } else if (scope.find("onvif://www.onvif.org/manufacturer/") == 0) {
            device.manufacturer = scope.substr(36);  // 移除前缀
        }
    }
    
    device.last_seen = std::chrono::system_clock::now();
    
    return device;
}

std::vector<std::string> ONVIFGateway::GetProfiles(const ONVIFDevice& device) {
    // 构建 GetProfiles SOAP 请求
    std::string soap_body = "<trt:GetProfiles/>";
    std::string soap_action = "http://www.onvif.org/ver10/media/wsdl/GetProfiles";
    
    // 获取 Media 服务地址（从 XAddr 推断）
    std::string media_url = device.xaddr;
    // 如果 XAddr 是设备服务地址，需要替换为 Media 服务地址
    // 通常格式: http://ip:port/onvif/device_service -> http://ip:port/onvif/media_service
    size_t pos = media_url.find("/onvif/device_service");
    if (pos != std::string::npos) {
        media_url.replace(pos, 21, "/onvif/media_service");
    } else {
        // 尝试其他可能的路径
        pos = media_url.find("/device_service");
        if (pos != std::string::npos) {
            media_url.replace(pos, 14, "/media_service");
        }
    }
    
    std::string response = SendSOAPRequest(media_url, soap_action, soap_body,
                                          device.username, device.password);
    
    if (response.empty()) {
        LOG_ERROR("GetProfiles 请求失败: {} (URL: {})", device.id, media_url);
        return {};
    }
    
    // 解析响应，提取 Profile Token
    std::vector<std::string> profile_tokens;
    
    // 使用正则表达式提取 Profile Token
    // 尝试多种可能的命名空间前缀
    // 注意：Python ElementTree 可能使用 ns1:, ns2: 等前缀，而不是标准的 trt:
    std::vector<std::regex> token_regexes = {
        std::regex(R"(<[^:]*:Profiles[^>]*token=\"([^\"]+)\")"),  // 任意命名空间前缀（最通用，优先）
        std::regex(R"(<ns1:Profiles[^>]*token=\"([^\"]+)\")"),    // Python ElementTree 常用前缀
        std::regex(R"(<trt:Profiles[^>]*token=\"([^\"]+)\")"),    // 标准 ONVIF 前缀
        std::regex(R"(Profiles[^>]*token=\"([^\"]+)\")")          // 最宽松的匹配
    };
    
    for (const auto& token_regex : token_regexes) {
        std::sregex_iterator iter(response.begin(), response.end(), token_regex);
        std::sregex_iterator end;
        
        for (; iter != end; ++iter) {
            profile_tokens.push_back(iter->str(1));
        }
        
        if (!profile_tokens.empty()) {
            break;  // 找到匹配就退出
        }
    }
    
    if (profile_tokens.empty()) {
        LOG_WARN("未找到 Profile Token: {} (响应长度: {} 字节)", device.id, response.length());
        LOG_INFO("GetProfiles 完整响应内容: {}", response);
    } else {
        LOG_INFO("找到 {} 个 Profile Token: {}", profile_tokens.size(), 
                 profile_tokens.empty() ? "" : profile_tokens[0]);
    }
    
    return profile_tokens;
}

std::string ONVIFGateway::GetStreamUri(const ONVIFDevice& device, const std::string& profile_token) {
    // 构建 GetStreamUri SOAP 请求
    std::ostringstream soap_body;
    soap_body << "<trt:GetStreamUri>"
              << "<trt:ProfileToken>" << profile_token << "</trt:ProfileToken>"
              << "<trt:StreamSetup>"
              << "<tt:Stream>RTP-Unicast</tt:Stream>"
              << "<tt:Transport>"
              << "<tt:Protocol>RTSP</tt:Protocol>"
              << "</tt:Transport>"
              << "</trt:StreamSetup>"
              << "</trt:GetStreamUri>";
    
    std::string soap_action = "http://www.onvif.org/ver10/media/wsdl/GetStreamUri";
    
    // 获取 Media 服务地址（从 XAddr 推断）
    std::string media_url = device.xaddr;
    // 如果 XAddr 是设备服务地址，需要替换为 Media 服务地址
    // 通常格式: http://ip:port/onvif/device_service -> http://ip:port/onvif/media_service
    // 优先检查 /onvif/device_service，避免误匹配
    size_t pos = media_url.find("/onvif/device_service");
    if (pos != std::string::npos) {
        media_url.replace(pos, 21, "/onvif/media_service");
    } else {
        // 尝试其他可能的路径
        pos = media_url.find("/device_service");
        if (pos != std::string::npos) {
            media_url.replace(pos, 14, "/media_service");
        }
    }
    
    std::string response = SendSOAPRequest(media_url, soap_action, soap_body.str(),
                                          device.username, device.password);
    
    if (response.empty()) {
        LOG_ERROR("GetStreamUri 请求失败: {} (profile: {}, URL: {})", device.id, profile_token, media_url);
        return "";
    }
    
    // 解析响应，提取 RTSP URI
    // 尝试多种可能的命名空间前缀
    // 注意：Python ElementTree 可能使用 ns1:, ns2: 等前缀，而不是标准的 trt:
    std::vector<std::regex> uri_regexes = {
        std::regex(R"(<[^:]*:Uri[^>]*>([^<]+)</[^:]*:Uri>)"),  // 任意命名空间前缀（最通用，优先）
        std::regex(R"(<ns1:Uri[^>]*>([^<]+)</ns1:Uri>)"),      // Python ElementTree 常用前缀
        std::regex(R"(<trt:Uri[^>]*>([^<]+)</trt:Uri>)"),      // 标准 ONVIF 前缀
        std::regex(R"(<Uri[^>]*>([^<]+)</Uri>)")               // 最宽松的匹配
    };
    
    for (const auto& uri_regex : uri_regexes) {
        std::smatch uri_match;
        if (std::regex_search(response, uri_match, uri_regex)) {
            return uri_match[1].str();
        }
    }
    
    LOG_WARN("未找到 RTSP URI: {} (profile: {})", device.id, profile_token);
    LOG_INFO("GetStreamUri 完整响应内容: {}", response);
    return "";
}

std::string ONVIFGateway::BuildSOAPRequest(const std::string& action, const std::string& body) {
    std::string message_id = GenerateUUID();
    
    std::ostringstream soap;
    soap << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
         << "<s:Envelope xmlns:s=\"http://www.w3.org/2003/05/soap-envelope\" "
         << "xmlns:a=\"http://schemas.xmlsoap.org/ws/2004/08/addressing\" "
         << "xmlns:trt=\"http://www.onvif.org/ver10/media/wsdl\" "
         << "xmlns:tt=\"http://www.onvif.org/ver10/schema\">\n"
         << "<s:Header>\n"
         << "<a:Action s:mustUnderstand=\"1\">" << action << "</a:Action>\n"
         << "<a:MessageID>uuid:" << message_id << "</a:MessageID>\n"
         << "<a:To s:mustUnderstand=\"1\">" << action << "</a:To>\n"
         << "</s:Header>\n"
         << "<s:Body>\n"
         << body
         << "\n</s:Body>\n"
         << "</s:Envelope>";
    
    return soap.str();
}

std::string ONVIFGateway::SendSOAPRequest(const std::string& url,
                                         const std::string& soap_action,
                                         const std::string& soap_body,
                                         const std::string& username,
                                         const std::string& password) {
    std::string soap_request = BuildSOAPRequest(soap_action, soap_body);
    
    CURL* curl = curl_easy_init();
    if (!curl) {
        LOG_ERROR("初始化 CURL 失败");
        return "";
    }
    
    // 设置响应回调
    std::string response_data;
    response_data.reserve(8192);
    
    // 设置 CURL 选项
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, soap_request.c_str());
    curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, soap_request.length());
    
    // 设置 HTTP 头
    struct curl_slist* headers = nullptr;
    headers = curl_slist_append(headers, "Content-Type: application/soap+xml; charset=utf-8");
    std::string action_header = "SOAPAction: \"" + soap_action + "\"";
    headers = curl_slist_append(headers, action_header.c_str());
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    
    // 设置认证
    if (!username.empty() && !password.empty()) {
        curl_easy_setopt(curl, CURLOPT_HTTPAUTH, CURLAUTH_DIGEST | CURLAUTH_BASIC);
        curl_easy_setopt(curl, CURLOPT_USERNAME, username.c_str());
        curl_easy_setopt(curl, CURLOPT_PASSWORD, password.c_str());
    }
    
    // 设置超时（增加超时时间，避免网络延迟导致的问题）
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 30L);  // 总超时 30 秒
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 10L);  // 连接超时 10 秒
    curl_easy_setopt(curl, CURLOPT_LOW_SPEED_LIMIT, 1L);  // 低速限制：1 字节/秒
    curl_easy_setopt(curl, CURLOPT_LOW_SPEED_TIME, 30L);  // 低速超时：30 秒
    
    // 禁用压缩和其他可能导致问题的特性
    curl_easy_setopt(curl, CURLOPT_ACCEPT_ENCODING, "");  // 禁用压缩
    curl_easy_setopt(curl, CURLOPT_HTTP_TRANSFER_DECODING, 0L);  // 禁用传输解码
    curl_easy_setopt(curl, CURLOPT_HTTP_CONTENT_DECODING, 0L);  // 禁用内容解码
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 0L);  // 禁用重定向
    curl_easy_setopt(curl, CURLOPT_MAXREDIRS, 0L);  // 最大重定向次数为 0
    
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, ::utils::http_callback::WriteCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response_data);
    
    try {
        CURLcode res = curl_easy_perform(curl);
        
        long response_code = 0;
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &response_code);
        
        curl_slist_free_all(headers);
        curl_easy_cleanup(curl);
        
        if (res != CURLE_OK) {
            LOG_ERROR("SOAP 请求失败: {} (CURL error: {})", url, curl_easy_strerror(res));
            return "";
        }
        
        if (response_code != 200) {
            LOG_ERROR("SOAP 请求返回错误状态码: {} (HTTP {})", url, response_code);
            return "";
        }
        
        LOG_DEBUG("SOAP 请求成功: {} (HTTP {}), 响应长度: {} 字节", url, response_code, response_data.length());
        
        return response_data;
    } catch (const std::exception& e) {
        LOG_ERROR("SOAP 请求异常: {} (异常信息: {})", url, e.what());
        curl_slist_free_all(headers);
        curl_easy_cleanup(curl);
        return "";
    } catch (...) {
        LOG_ERROR("SOAP 请求未知异常: {}", url);
        curl_slist_free_all(headers);
        curl_easy_cleanup(curl);
        return "";
    }
}

std::string ONVIFGateway::GenerateDeviceID(const std::string& xaddr) const {
    // 基于 XAddr 生成设备 ID
    // 简单实现：使用 XAddr 的哈希值
    std::hash<std::string> hasher;
    size_t hash = hasher(xaddr);
    std::ostringstream oss;
    oss << "onvif_device_" << std::hex << hash;
    return oss.str();
}

std::string ONVIFGateway::GenerateUUID() const {
    // 生成简单的 UUID（格式: xxxxxxxx-xxxx-xxxx-xxxx-xxxxxxxxxxxx）
    static std::random_device rd;
    static std::mt19937 gen(rd());
    static std::uniform_int_distribution<> dis(0, 15);
    static std::uniform_int_distribution<> dis2(8, 11);
    
    std::ostringstream oss;
    oss << std::hex;
    for (int i = 0; i < 8; i++) {
        oss << dis(gen);
    }
    oss << "-";
    for (int i = 0; i < 4; i++) {
        oss << dis(gen);
    }
    oss << "-4";
    for (int i = 0; i < 3; i++) {
        oss << dis(gen);
    }
    oss << "-";
    oss << dis2(gen);
    for (int i = 0; i < 3; i++) {
        oss << dis(gen);
    }
    oss << "-";
    for (int i = 0; i < 12; i++) {
        oss << dis(gen);
    }
    
    return oss.str();
}

} // namespace gateway

