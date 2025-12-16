#include "gb28181_gateway.hpp"
#include "sip_server.hpp"
#include "sip_message.hpp"
#include "config/config_loader.hpp"
#include "config/constants.hpp"
#include "utils/logger.hpp"
#include "utils/url_parser.hpp"
#include "utils/stream_status_checker.hpp"
#include "gateway/utils/error_codes.hpp"
#include "gateway/utils/error_inference.hpp"
#include <sstream>
#include <algorithm>
#include <regex>
#include <thread>
#include <chrono>

using namespace config::constants;
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <cstring>


#include "gateway/utils/gateway_registry.hpp"

namespace gateway {

namespace {
static gateway::utils::AutoRegister reg("gb28181", [](const gateway::utils::GatewayContext& ctx) -> std::shared_ptr<gateway::GatewayBase> {
    if (ctx.config->gb28181.enabled) {
        return std::make_shared<GB28181Gateway>(ctx.config, ctx.zlm_client, ctx.stream_manager);
    }
    return nullptr;
});
}

GB28181Gateway::GB28181Gateway(
    std::shared_ptr<config::Config> config,
    std::shared_ptr<streaming::ZLMClient> zlm_client,
    std::shared_ptr<streaming::StreamManager> stream_manager)
    : config_(config)
    , zlm_client_(zlm_client)
    , stream_manager_(stream_manager)
    , heartbeat_check_running_(false)
    , stream_status_check_running_(false) {
    
    // 从配置中读取GB28181配置
    if (config_) {
        server_id_ = config_->gb28181.server_id;
        domain_ = config_->gb28181.domain;
        heartbeat_timeout_seconds_ = config_->gb28181.heartbeat_timeout_seconds;
        auto_remove_offline_ = config_->gb28181.auto_remove_offline;
        default_tcp_mode_ = config_->gb28181.default_tcp_mode;
        default_enable_rtcp_ = config_->gb28181.default_enable_rtcp;
        rtp_port_range_ = config_->gb28181.rtp_port_range;
        stream_start_timeout_seconds_ = config_->gb28181.stream_start_timeout_seconds;
    } else {
        // 使用默认值
        server_id_ = "34020000002000000001";
        domain_ = "3402000000";
        heartbeat_timeout_seconds_ = 300;
        auto_remove_offline_ = false;
        default_tcp_mode_ = 0;
        default_enable_rtcp_ = false;
        rtp_port_range_ = "30000-35000";
        stream_start_timeout_seconds_ = 60;
    }
    
    // 创建SIP服务器
    std::string local_ip = config_ ? config_->gb28181.local_ip : "0.0.0.0";
    int local_port = config_ ? config_->gb28181.local_port : 5060;
    
    sip_server_ = std::make_unique<SipServer>(local_ip, local_port, server_id_, domain_);
    
    // 设置SIP服务器回调
    sip_server_->SetDeviceRegisterCallback(
        [this](const std::string& device_id, const std::string& ip, int port, int expires) {
            OnDeviceRegister(device_id, ip, port, expires);
        });
    
    sip_server_->SetDeviceHeartbeatCallback(
        [this](const std::string& device_id) {
            OnDeviceHeartbeat(device_id);
        });
    
    // 设置设备认证回调
    sip_server_->SetDeviceAuthCallback(
        [this](const std::string& device_id, std::string& username, std::string& password) -> bool {
            GB28181Device device = GetDevice(device_id);
            if (!device.id.empty() && !device.username.empty() && !device.password.empty()) {
                username = device.username;
                password = device.password;
                return true;  // 需要认证
            }
            return false;  // 不需要认证
        });
    
    // 设置响应消息回调（处理设备对INVITE请求的响应）
    sip_server_->SetResponseCallback(
        [this](const std::string& call_id, int status_code, const std::string& reason_phrase, int peer_port) {
            OnSipResponse(call_id, status_code, reason_phrase, peer_port);
        });
    
    // 设置BYE请求回调
    sip_server_->SetByeRequestCallback(
        [this](const std::string& call_id, const std::string& rtp_stream_id) {
            return OnByeRequest(call_id);
        });
    
    // 自动启动SIP服务器（如果GB28181已启用）
    // 这样设备可以在没有启动流的情况下注册
    bool gb28181_enabled = config_ ? config_->gb28181.enabled : true;
    if (gb28181_enabled) {
        if (StartSipServer()) {
            LOG_INFO("GB28181 Gateway: SIP服务器已自动启动，端口: {}", local_port);
        } else {
            LOG_ERROR("GB28181 Gateway: SIP服务器自动启动失败");
        }
    }
}

GB28181Gateway::~GB28181Gateway() {
    // 先停止线程
    heartbeat_check_running_ = false;
    if (heartbeat_check_thread_.joinable()) {
        heartbeat_check_thread_.join();
    }
    
    stream_status_check_running_ = false;
    if (stream_status_check_thread_.joinable()) {
        stream_status_check_thread_.join();
    }
    
    // 停止SIP服务器（会再次检查并停止线程，但线程已经停止，所以安全）
    StopSipServer();
    
    // 停止所有流（使用GatewayBase::StopAllStreamsNative模式，但需要处理GB28181特有逻辑）
    std::lock_guard<std::mutex> lock(streams_mutex_);
    for (auto& pair : streams_) {
        auto& info = pair.second;
        // 发送BYE请求（GB28181特有逻辑）
        // 优先使用StreamInfo中保存的设备SIP端口（从设备注册时保存的实际端口）
        if (!info.call_id.empty()) {
            GB28181Device device = GetDevice(info.device_id);
            if (!device.id.empty()) {
                int device_port = info.device_sip_port > 0 ? info.device_sip_port : device.port;
                LOG_DEBUG("GB28181 Gateway: 析构时发送BYE请求，使用端口: {} (StreamInfo.device_sip_port={}, device.port={})", 
                         device_port, info.device_sip_port, device.port);
                SendBye(info.call_id, device.ip, device_port);
            }
        }
        // 关闭RTP服务器（GB28181特有逻辑）
        if (!info.rtp_stream_id.empty()) {
            zlm_client_->CloseRtpServer(info.rtp_stream_id);
        }
        // 从StreamManager中注销
        if (stream_manager_) {
            stream_manager_->UnregisterStream(info.target_app, info.target_stream);
        }
    }
    streams_.clear();
}

bool GB28181Gateway::StartSipServer() {
    if (sip_server_started_) {
        return true;
    }
    
    if (!sip_server_->Start()) {
        LOG_ERROR("GB28181 Gateway: 启动SIP服务器失败");
        return false;
    }
    
    sip_server_started_ = true;
    
    // 启动心跳检查线程
    heartbeat_check_running_ = true;
    heartbeat_check_thread_ = std::thread(&GB28181Gateway::CheckDeviceHeartbeat, this);
    
    // 启动流状态检查线程
    stream_status_check_running_ = true;
    stream_status_check_thread_ = std::thread(&GB28181Gateway::CheckStreamStatus, this);
    
    return true;
}

void GB28181Gateway::StopSipServer() {
    if (!sip_server_started_) {
        return;
    }
    
    heartbeat_check_running_ = false;
    if (heartbeat_check_thread_.joinable()) {
        heartbeat_check_thread_.join();
    }
    
    stream_status_check_running_ = false;
    if (stream_status_check_thread_.joinable()) {
        stream_status_check_thread_.join();
    }
    
    sip_server_->Stop();
    sip_server_started_ = false;
    
    LOG_INFO("GB28181 Gateway SIP服务器已停止");
}

Result<void> GB28181Gateway::Start(const std::string& source_url,
                          const std::string& target_app,
                          const std::string& target_stream,
                          const std::string& output_protocol) {
    // 通过 StreamManager 统一记录"创建请求已发起"，状态设为 Starting
    if (stream_manager_) {
        streaming::StreamMetadata metadata;
        metadata.app = target_app;
        metadata.stream = target_stream;
        metadata.protocol = "gb28181";
        metadata.output_protocol = output_protocol;
        metadata.source_url = source_url;
        metadata.gateway_type = "gb28181_gateway";
        metadata.status = streaming::StreamStatus::Starting;
        stream_manager_->OnStreamCreateRequested(metadata);
    }
    
    // 确保SIP服务器已启动
    if (!sip_server_started_) {
        LOG_INFO("GB28181 Gateway: 检测到SIP服务器未启动，正在启动...");
        if (!StartSipServer()) {
            LOG_ERROR("GB28181 Gateway: 无法启动SIP服务器");
            if (stream_manager_) {
                using namespace gateway::utils;
                stream_manager_->OnStreamCreateResult(target_app, target_stream, false,
                                                     ErrorCode::ZLM_API_FAILED,
                                                     ErrorMessageMapper::GetMessage(ErrorCode::ZLM_API_FAILED));
            }
            return Result<void>::Failure(gateway::InternalServerException("Failed to start SIP server"));
        }
        LOG_INFO("GB28181 Gateway: SIP服务器启动成功");
    } else {
        LOG_DEBUG("GB28181 Gateway: SIP服务器已在运行");
    }
    
    // 解析source_url（使用统一的URL解析工具）
    if (!::utils::url_parser::ValidateURL(source_url, "gb28181://")) {
        LOG_ERROR("GB28181 Gateway: 无效的源 URL 格式: {}", source_url);
        if (stream_manager_) {
            using namespace gateway::utils;
            std::string error_msg = "无效的源 URL 格式";
            auto [error_code, error_message] = InferErrorFromMessage(error_msg);
            stream_manager_->OnStreamCreateResult(target_app, target_stream, false,
                                                 error_code, error_message);
        }
        return Result<void>::Failure(gateway::InvalidParameterException("Invalid source URL format: " + source_url));
    }
    
    auto [device_id, channel_id] = ::utils::url_parser::ParseDeviceURL(source_url, "gb28181://");
    if (device_id.empty() || channel_id.empty()) {
        LOG_ERROR("GB28181 Gateway: 无效的source_url（缺少设备ID或通道ID）: {}", source_url);
        if (stream_manager_) {
            using namespace gateway::utils;
            std::string error_msg = "无效的source_url（缺少设备ID或通道ID）";
            auto [error_code, error_message] = InferErrorFromMessage(error_msg);
            stream_manager_->OnStreamCreateResult(target_app, target_stream, false,
                                                 error_code, error_message);
        }
        return Result<void>::Failure(gateway::InvalidParameterException("Invalid source URL (missing device/channel ID): " + source_url));
    }
    
    LOG_DEBUG("GB28181 Gateway: 准备启动流，设备ID={}, 通道ID={}, target_app={}, target_stream={}", 
             device_id, channel_id, target_app, target_stream);
    
    return StartDeviceStream(device_id, channel_id, target_app, target_stream, output_protocol, source_url);
}

Result<void> GB28181Gateway::StartDeviceStream(const std::string& device_id,
                                      const std::string& channel_id,
                                      const std::string& target_app,
                                      const std::string& target_stream,
                                      const std::string& output_protocol,
                                      const std::string& source_url) {
    LOG_DEBUG("GB28181 Gateway: StartDeviceStream 开始，设备ID={}, 通道ID={}, target_app={}, target_stream={}", 
             device_id, channel_id, target_app, target_stream);
    
    // 验证设备是否存在和在线
    GB28181Device device = GetDevice(device_id);
    if (device.id.empty()) {
        // 获取设备数量用于调试
    std::lock_guard<std::mutex> lock(devices_mutex_);
    size_t device_count = devices_.size();
    lock.~lock_guard();  // 手动释放锁
    LOG_ERROR("GB28181 Gateway: 设备不存在: {} (当前已注册设备数量: {})", device_id, device_count);
        if (stream_manager_) {
            using namespace gateway::utils;
            stream_manager_->OnStreamCreateResult(target_app, target_stream, false,
                                                 ErrorCode::DEVICE_NOT_FOUND,
                                                 ErrorMessageMapper::GetMessage(ErrorCode::DEVICE_NOT_FOUND));
        }
        return Result<void>::Failure(gateway::DeviceNotFoundException("Device not found: " + device_id));
    }
    
    LOG_DEBUG("GB28181 Gateway: 设备已找到，设备ID={}, IP={}, 端口={}, 在线={}", 
             device.id, device.ip, device.port, device.online);
    
    if (!device.online) {
        LOG_WARN("GB28181 Gateway: 设备离线，但允许尝试启动流（用于测试）: {}", device_id);
        // 对于测试场景，允许离线设备也能尝试启动流
        // 但会记录警告，实际启动可能会失败
        // 注意：真实场景中，设备应该先通过SIP REGISTER注册
    }
    
    // 检查流是否已存在（使用GatewayBase的统一方法）
    std::string stream_id = GenerateStreamId(target_app, target_stream);
    auto [exists, is_running] = GatewayBase::CheckStreamExists<StreamInfo>(
        stream_id, streams_, streams_mutex_, target_app, target_stream,
        [this](StreamInfo& info) {
            // 清理旧流：发送BYE请求、关闭RTP服务器
            // 优先使用StreamInfo中保存的设备SIP端口（从设备注册时保存的实际端口）
            if (!info.call_id.empty()) {
                GB28181Device device = GetDevice(info.device_id);
                if (!device.id.empty()) {
                    int device_port = info.device_sip_port > 0 ? info.device_sip_port : device.port;
                    SendBye(info.call_id, device.ip, device_port);
                }
            }
            if (!info.rtp_stream_id.empty() && zlm_client_) {
                try {
                    zlm_client_->CloseRtpServer(info.rtp_stream_id);
                } catch (const std::exception& e) {
                    LOG_WARN("GB28181 Gateway: 关闭旧RTP服务器失败: {} - {}", info.rtp_stream_id, e.what());
                }
            }
            info.status = GatewayStatus::Stopped;
        });
    
    if (exists && is_running) {
        if (stream_manager_) {
            stream_manager_->OnStreamCreateResult(target_app, target_stream, true);
        }
        return Result<void>::Success();
    }
    
    // 创建流信息（与其他Gateway保持一致的模式）
    StreamInfo info;
    info.device_id = device_id;
    info.channel_id = channel_id;
    info.source_url = source_url;
    info.target_app = target_app;
    info.target_stream = target_stream;
    info.output_protocol = output_protocol;
    info.status = GatewayStatus::Starting;
    info.created_time = std::chrono::system_clock::now();  // 记录创建时间
    
    // 创建RTP服务器（带重试机制，使用配置中的重试参数）
    // 注意：ZLM的stream_id应该使用唯一的流标识
    // 为了避免"This stream already exists"错误，使用带时间戳的stream_id
    auto now = std::chrono::system_clock::now();
    auto timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(
        now.time_since_epoch()).count();
    std::string rtp_stream_id = target_stream + "_" + std::to_string(timestamp);
    
    // 先检查并关闭可能已存在的RTP服务器（避免"This stream already exists"错误）
    // 注意：ZLM的stream_id可能只包含app名称，需要检查所有可能的匹配
    // 重要：在持有锁之前执行ZLM API调用，避免阻塞其他操作
    try {
        // 列出所有RTP服务器，查找匹配的（包括完整stream_id和app名称）
        auto existing_servers = zlm_client_->ListRtpServer();
        bool found_existing = false;
        std::string found_stream_id;
        for (const auto& server : existing_servers) {
            // 检查完全匹配或app名称匹配
            if (server.stream_id == rtp_stream_id || 
                server.stream_id == target_app ||
                rtp_stream_id.find(server.stream_id) != std::string::npos ||
                server.stream_id.find(target_app) != std::string::npos) {
                found_existing = true;
                found_stream_id = server.stream_id;
                LOG_INFO("GB28181 Gateway: 发现可能匹配的RTP服务器: server_stream_id={}, our_stream_id={}, port={}", 
                        server.stream_id, rtp_stream_id, server.port);
                // 尝试关闭所有匹配的服务器
                zlm_client_->CloseRtpServer(server.stream_id, target_app, "__defaultVhost__");
            }
        }
        
        // 也尝试直接关闭我们想要的stream_id（即使ListRtpServer没找到）
        LOG_DEBUG("GB28181 Gateway: 强制尝试关闭RTP服务器: {}", rtp_stream_id);
        zlm_client_->CloseRtpServer(rtp_stream_id, target_app, "__defaultVhost__");
        
        if (found_existing) {
            LOG_INFO("GB28181 Gateway: 已尝试关闭匹配的RTP服务器");
        } else {
            LOG_DEBUG("GB28181 Gateway: 未发现已存在的RTP服务器: {}", rtp_stream_id);
        }
        
        // 等待关闭完成（ZLM可能需要时间清理）
        std::this_thread::sleep_for(std::chrono::milliseconds(time::GB28181_INVITE_WAIT_MS));
        
        // 再次检查是否已关闭
        auto servers_after_close = zlm_client_->ListRtpServer();
        bool still_exists = false;
        for (const auto& server : servers_after_close) {
            if (server.stream_id == rtp_stream_id || 
                server.stream_id == target_app ||
                rtp_stream_id.find(server.stream_id) != std::string::npos) {
                still_exists = true;
                LOG_WARN("GB28181 Gateway: RTP服务器仍然存在: server_stream_id={}, our_stream_id={}", 
                        server.stream_id, rtp_stream_id);
                // 再次尝试关闭
                zlm_client_->CloseRtpServer(server.stream_id, target_app, "__defaultVhost__");
            }
        }
        if (!still_exists) {
            LOG_DEBUG("GB28181 Gateway: RTP服务器已成功关闭: {}", rtp_stream_id);
        } else {
            // 如果仍然存在，再等待一段时间
            std::this_thread::sleep_for(std::chrono::milliseconds(time::GB28181_BYE_WAIT_MS));
        }
    } catch (const std::exception& e) {
        LOG_WARN("GB28181 Gateway: 检查/关闭RTP服务器时出现异常: {}，将继续尝试创建", e.what());
    }
    
    // 创建RTP服务器（在锁外执行，避免阻塞）
    streaming::RtpServerInfo rtp_info;
    if (!CreateRtpServerWithRetry(rtp_stream_id, rtp_info, target_app)) {
        // 错误处理：设置status为Error并保存到streams_（与其他Gateway保持一致）
        std::lock_guard<std::mutex> lock(streams_mutex_);
        info.status = GatewayStatus::Error;
        streams_[stream_id] = info;
        if (stream_manager_) {
            using namespace gateway::utils;
            stream_manager_->OnStreamCreateResult(target_app, target_stream, false,
                                                 ErrorCode::ZLM_API_FAILED,
                                                 ErrorMessageMapper::GetMessage(ErrorCode::ZLM_API_FAILED));
        }
        return Result<void>::Failure(gateway::ZLMAPIException("Failed to create RTP server"));
    }
    
    // 保存实际使用的stream_id
    // 重要：使用ZLM返回的实际stream_id（可能和传入的rtp_stream_id不同）
    // 这确保了rtp_stream_id和ZLM中实际注册的流名称一致
    info.rtp_stream_id = rtp_info.stream_id.empty() ? rtp_stream_id : rtp_info.stream_id;
    info.rtp_port = rtp_info.port;
    
    // 如果ZLM返回的stream_id和传入的不同，记录日志
    if (!rtp_info.stream_id.empty() && rtp_info.stream_id != rtp_stream_id) {
        LOG_INFO("GB28181 Gateway: ZLM返回的实际流名称与传入的不同: 传入={}, ZLM返回={}", 
                rtp_stream_id, rtp_info.stream_id);
    }
    
    // 发送SIP INVITE请求（在锁外执行，避免阻塞）
    std::string call_id;
    if (!SendInvite(device, channel_id, rtp_info, call_id)) {
        zlm_client_->CloseRtpServer(rtp_stream_id);
        // 错误处理：设置status为Error并保存到streams_（与其他Gateway保持一致）
        std::lock_guard<std::mutex> lock(streams_mutex_);
        info.status = GatewayStatus::Error;
        streams_[stream_id] = info;
        if (stream_manager_) {
            std::string error_msg = "发送SIP INVITE失败";
            using namespace gateway::utils;
            auto [error_code, error_message] = InferErrorFromMessage(error_msg);
            stream_manager_->OnStreamCreateResult(target_app, target_stream, false,
                                                 error_code, error_message);
        }
        return Result<void>::Failure(gateway::SIPException("Failed to send SIP INVITE"));
    }
    
    info.call_id = call_id;
    // 保存设备的SIP端口（从设备注册时保存的实际端口，用于发送BYE请求）
    info.device_sip_port = device.port;
    
    // 现在加锁，保存流信息
    std::lock_guard<std::mutex> lock(streams_mutex_);
    
    // 更新流信息（状态保持为Starting，等待设备响应200 OK后更新为Running）
    info.status = GatewayStatus::Starting;
    streams_[stream_id] = info;
    
    // 记录流创建的详细信息，用于调试超时问题
    auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now() - info.created_time).count();
    LOG_INFO("GB28181 Gateway: 流创建完成，等待设备响应: {}/{} (Call-ID: {}, 设备: {}, 通道: {}, RTP端口: {}, 已耗时: {}ms, 超时时间: {}秒)", 
            target_app, target_stream, call_id, device_id, channel_id, rtp_info.port, 
            elapsed_ms, stream_start_timeout_seconds_);
    
    // 注册流到StreamManager（使用GatewayBase的统一方法，状态为Starting）
    // 重要：使用rtp_stream_id作为stream名称注册，这样ZLM的hook通知时能正确匹配
    // 因为ZLM中的流名称是rtp_stream_id（包含时间戳），而不是target_stream
    if (stream_manager_) {
        // 注意：这里使用target_stream注册，但ZLM中的实际流名称是rtp_stream_id
        // 当ZLM的on_stream_changed hook被调用时，会使用rtp_stream_id作为stream参数
        // 所以我们需要在hook处理时进行匹配，或者在这里使用rtp_stream_id注册
        // 但是，为了保持一致性，我们仍然使用target_stream注册
        // 在收到hook通知时，我们需要检查rtp_stream_id是否匹配
        GatewayBase::RegisterStreamToManager(
            stream_manager_,
            target_app,
            target_stream,  // 使用target_stream注册，但ZLM中的流名称是rtp_stream_id
            "gb28181",
            output_protocol,
            source_url,
            "gb28181_gateway",
            GatewayStatus::Starting,
            0,  // 不使用FFmpeg进程
            device_id,
            "gb28181"
        );
        
        // 同时使用rtp_stream_id注册一个映射，这样hook通知时能正确匹配
        // 注意：这需要StreamManager支持一个流有多个名称的映射
        // 或者，我们可以在hook处理时进行匹配
        // 暂时先不实现，等待hook通知时再处理
    }
    
    // 注意：不立即调用OnStreamCreateResult，等待ZLM的hook通知或200 OK响应后再调用
    // 如果超时未收到响应，流状态检查线程会处理
    
    return Result<void>::Success();
}

bool GB28181Gateway::CreateRtpServerWithRetry(const std::string& rtp_stream_id,
                                             streaming::RtpServerInfo& rtp_info,
                                             const std::string& target_app) {
    // 从配置中读取重试参数（与其他Gateway保持一致）
    int max_retries = config_ ? config_->gateway.direct_proxy.max_retries : 5;
    int retry_interval_ms = config_ ? config_->gateway.direct_proxy.retry_interval_ms : 2000;
    
    // 使用target_app作为ZLM的app参数，这样流会在正确的app下注册
    // 注意：如果target_app为空，使用默认值"gb28181"
    std::string app = target_app.empty() ? "gb28181" : target_app;
    
    for (int i = 0; i < max_retries; ++i) {
        // 传递local_ip参数，确保RTP服务器绑定到正确的IP地址
        // 如果local_ip为空或"0.0.0.0"，ZLM会绑定到0.0.0.0（所有接口）
        // 这样可以接收来自127.0.0.1的数据包
        std::string local_ip = config_ ? config_->gb28181.local_ip : "0.0.0.0";
        rtp_info = zlm_client_->OpenRtpServer(
            rtp_stream_id,
            0,  // 自动分配端口
            default_tcp_mode_,
            default_enable_rtcp_,
            local_ip,  // 传递local_ip参数
            "",  // ssrc参数
            app  // 传递target_app作为ZLM的app参数
        );
        
        if (rtp_info.port > 0) {
            LOG_INFO("GB28181 Gateway: 创建RTP服务器成功: stream_id={}, port={}", 
                    rtp_stream_id, rtp_info.port);
            return true;
        }
        
        // 检查错误消息，如果是"This stream already exists"，需要更激进的清理
        std::string error_msg;
        // 注意：OpenRtpServer返回的RtpServerInfo中没有错误消息字段
        // 我们需要通过port==0来判断失败，并通过日志中的错误消息来判断原因
        
        // 如果失败且不是最后一次重试，尝试关闭可能存在的服务器
        if (i < max_retries - 1) {
            LOG_WARN("GB28181 Gateway: 创建RTP服务器失败，重试 {}/{}", i + 1, max_retries);
            
            // 在重试前，强制尝试关闭可能存在的服务器（即使ListRtpServer没找到）
            // 因为ZLM可能内部状态不一致，需要多次尝试关闭并等待更长时间
            try {
                LOG_DEBUG("GB28181 Gateway: 重试前强制关闭RTP服务器: {}", rtp_stream_id);
                
                // 多次尝试关闭，确保ZLM内部状态清理
                for (int close_attempt = 0; close_attempt < 5; ++close_attempt) {
                    zlm_client_->CloseRtpServer(rtp_stream_id, app, "__defaultVhost__");
                    std::this_thread::sleep_for(std::chrono::milliseconds(time::GB28181_RETRY_INTERVAL_MS));  // 每次等待
                    
                    // 检查是否已关闭
                    auto servers = zlm_client_->ListRtpServer();
                    bool found = false;
                    for (const auto& server : servers) {
                        if (server.stream_id == rtp_stream_id) {
                            found = true;
                            LOG_DEBUG("GB28181 Gateway: RTP服务器仍存在（尝试 {}/5），继续关闭: {}", 
                                    close_attempt + 1, rtp_stream_id);
                            break;
                        }
                    }
                    if (!found) {
                        LOG_DEBUG("GB28181 Gateway: RTP服务器已成功关闭: {}", rtp_stream_id);
                        break;
                    }
                }
            } catch (const std::exception& e) {
                LOG_DEBUG("GB28181 Gateway: 重试前关闭RTP服务器时出现异常: {}", e.what());
            }
            
            // 等待更长时间，确保ZLM内部状态完全清理
            std::this_thread::sleep_for(std::chrono::milliseconds(retry_interval_ms + time::GB28181_RETRY_INTERVAL_MS));
        }
    }
    
    // 使用错误推断工具
    std::string error_msg = "创建RTP服务器失败（已重试" + std::to_string(max_retries) + "次）";
    using namespace gateway::utils;
    auto [error_code, error_message] = InferErrorFromMessage(error_msg);
    LOG_ERROR("GB28181 Gateway: {} ({})", error_message, error_code);
    return false;
}

Result<void> GB28181Gateway::Stop(const std::string& target_app,
                         const std::string& target_stream) {
    // 注意：target_stream可能是基础名称（不包含时间戳）或完整名称（包含时间戳）
    // 需要处理两种情况：
    // 1. target_stream是基础名称：34020000001320000001_34020000001320000001
    // 2. target_stream是完整名称：34020000001320000001_34020000001320000001_1765384946019
    
    // 首先尝试去掉时间戳，获取基础名称
    std::string base_stream = target_stream;
    size_t last_underscore = target_stream.find_last_of('_');
    if (last_underscore != std::string::npos && last_underscore < target_stream.length() - 1) {
        // 检查最后一个下划线后的部分是否是时间戳（纯数字，长度>=10）
        std::string possible_timestamp = target_stream.substr(last_underscore + 1);
        bool is_timestamp = !possible_timestamp.empty() && 
                           possible_timestamp.length() >= 10 &&
                           std::all_of(possible_timestamp.begin(), possible_timestamp.end(), ::isdigit);
        if (is_timestamp) {
            base_stream = target_stream.substr(0, last_underscore);
            LOG_DEBUG("GB28181 Gateway: Stop - 检测到时间戳，基础流名称: {} -> {}", target_stream, base_stream);
        }
    }
    
    std::string stream_id = GenerateStreamId(target_app, base_stream);
    
    // 先获取流信息（用于GB28181特有逻辑）
    // 优化：先收集候选流信息，然后释放锁，避免长时间持有锁
    StreamInfo info;
    std::string actual_stream_id;
    std::vector<std::pair<std::string, StreamInfo>> candidate_streams;  // <stream_id, StreamInfo>
    
    {
        std::lock_guard<std::mutex> lock(streams_mutex_);
        auto it = streams_.find(stream_id);
        if (it != streams_.end()) {
            info = it->second;
            actual_stream_id = stream_id;
        } else {
            // 直接查找失败，收集所有候选流信息
            for (const auto& pair : streams_) {
                const StreamInfo& stream_info = pair.second;
                if (stream_info.target_app == target_app) {
                    candidate_streams.push_back({pair.first, stream_info});
                }
            }
        }
    } // 锁在这里释放
    
    // 如果直接查找失败，在锁外进行匹配检查
    if (actual_stream_id.empty() && !candidate_streams.empty()) {
        LOG_DEBUG("GB28181 Gateway: 直接查找失败，尝试匹配流名称: {}/{} (base: {})", target_app, target_stream, base_stream);
        for (const auto& candidate : candidate_streams) {
            const StreamInfo& stream_info = candidate.second;
            // 检查target_stream是否匹配rtp_stream_id（完整名称）
            if (stream_info.rtp_stream_id == target_stream) {
                info = stream_info;
                actual_stream_id = candidate.first;
                LOG_DEBUG("GB28181 Gateway: 匹配成功（rtp_stream_id == target_stream），找到流: {}", target_stream);
                break;
            }
            // 检查base_stream是否匹配rtp_stream_id（去掉时间戳后）
            std::string rtp_base = stream_info.rtp_stream_id;
            if (!rtp_base.empty()) {
                size_t rtp_last_underscore = rtp_base.find_last_of('_');
                if (rtp_last_underscore != std::string::npos) {
                    std::string rtp_without_timestamp = rtp_base.substr(0, rtp_last_underscore);
                    if (rtp_without_timestamp == base_stream || rtp_without_timestamp == target_stream) {
                        info = stream_info;
                        actual_stream_id = candidate.first;
                        LOG_DEBUG("GB28181 Gateway: 匹配成功（去掉时间戳），找到流: {} -> {}", target_stream, rtp_base);
                        break;
                    }
                }
            }
            // 检查target_stream或base_stream是否匹配target_stream
            if (stream_info.target_stream == target_stream || stream_info.target_stream == base_stream) {
                info = stream_info;
                actual_stream_id = candidate.first;
                LOG_DEBUG("GB28181 Gateway: 完全匹配成功，找到流: {}", target_stream);
                break;
            }
        }
    }
    
    // 如果仍然找不到流，尝试从ZLM查询（在锁外执行，避免阻塞）
    if (actual_stream_id.empty()) {
        LOG_DEBUG("GB28181 Gateway: streams_ map中未找到流，尝试从ZLM查询: {}/{} (base: {})", target_app, target_stream, base_stream);
        if (zlm_client_) {
            try {
                // 尝试获取rtp_stream_id（可能从ZLM查询到）
                // 注意：GetRtpStreamId内部会加锁，但不会调用ZLM API（已禁用），所以不会阻塞
                std::string rtp_stream_id = GetRtpStreamId(target_app, base_stream);
                if (!rtp_stream_id.empty()) {
                    // 找到了rtp_stream_id，尝试关闭RTP服务器
                    LOG_INFO("GB28181 Gateway: 从ZLM查询到流，尝试关闭RTP服务器: {}", rtp_stream_id);
                    try {
                        zlm_client_->CloseRtpServer(rtp_stream_id);
                        LOG_INFO("GB28181 Gateway: 已关闭RTP服务器（流不在Gateway streams_ map中）: {}", rtp_stream_id);
                    } catch (const std::exception& e) {
                        LOG_WARN("GB28181 Gateway: 关闭RTP服务器失败: {} - {}", rtp_stream_id, e.what());
                    }
                    // 从StreamManager注销流
                    if (stream_manager_) {
                        stream_manager_->UnregisterStream(target_app, base_stream);
                        if (target_stream != base_stream) {
                            stream_manager_->UnregisterStream(target_app, target_stream);
                        }
                    }
                    return Result<void>::Success();  // 即使streams_ map中没有，也返回成功（因为已清理ZLM）
                }
            } catch (const std::exception& e) {
                LOG_WARN("GB28181 Gateway: 从ZLM查询流信息时出现异常: {}", e.what());
            }
        }
        LOG_WARN("GB28181 Gateway: 流不存在（Gateway streams_ map和ZLM中都未找到）: {}/{} (base: {})", target_app, target_stream, base_stream);
        return Result<void>::Success();
    }
    
    // GB28181特有逻辑：发送SIP BYE请求
    // 优先使用StreamInfo中保存的设备SIP端口（从设备注册时保存的实际端口）
    // 如果不存在，再使用device.port（可能是配置中的端口，可能不正确）
    if (!info.call_id.empty()) {
        GB28181Device device = GetDevice(info.device_id);
        if (!device.id.empty()) {
            int device_port = info.device_sip_port > 0 ? info.device_sip_port : device.port;
            LOG_DEBUG("GB28181 Gateway: 发送BYE请求，使用端口: {} (StreamInfo.device_sip_port={}, device.port={})", 
                     device_port, info.device_sip_port, device.port);
            SendBye(info.call_id, device.ip, device_port);
        }
    }
    
    // GB28181特有逻辑：关闭RTP服务器
    bool success = true;
    if (!info.rtp_stream_id.empty()) {
        try {
            zlm_client_->CloseRtpServer(info.rtp_stream_id);
        } catch (const std::exception& e) {
            LOG_WARN("GB28181 Gateway: 关闭RTP服务器失败: {} - {}", info.rtp_stream_id, e.what());
            success = false;
        }
    }
    
    // 从StreamManager注销流（使用base_stream，因为StreamManager中存储的是基础名称）
    if (stream_manager_) {
        stream_manager_->UnregisterStream(target_app, base_stream);
        // 如果target_stream和base_stream不同，也尝试注销target_stream（以防万一）
        if (target_stream != base_stream) {
            stream_manager_->UnregisterStream(target_app, target_stream);
        }
    }
    
    // 从流列表中删除（使用actual_stream_id，如果匹配失败则使用stream_id）
    {
        std::lock_guard<std::mutex> lock(streams_mutex_);
        if (!actual_stream_id.empty()) {
            streams_.erase(actual_stream_id);
        } else {
            streams_.erase(stream_id);
        }
    }
    
    if (success) {
        LOG_INFO("GB28181 Gateway 停止成功: {}/{}", target_app, target_stream);
    } else {
        LOG_WARN("GB28181 Gateway 停止失败（但已清理本地记录）: {}/{}", target_app, target_stream);
    }
    
    return Result<void>::Success();
}

bool GB28181Gateway::IsRunning(const std::string& target_app,
                               const std::string& target_stream) {
    std::string stream_id = GenerateStreamId(target_app, target_stream);
    
    std::lock_guard<std::mutex> lock(streams_mutex_);
    
    auto it = streams_.find(stream_id);
    if (it == streams_.end()) {
        return false;
    }

    StreamInfo& info = it->second;
    
    // 使用工具类检查流状态（GB28181没有进程，pid=0）
    gateway::GatewayStatus new_status;
    int dummy_pid = 0;  // GB28181不使用进程，pid固定为0
    int new_pid;
    bool is_running = ::utils::StreamStatusChecker::CheckIsRunning(
        dummy_pid,  // GB28181不使用进程，pid=0
        info.status,
        zlm_client_,
        target_app,
        target_stream,
        new_status,
        new_pid
    );
    
    info.status = new_status;
    // GB28181不使用进程，不需要更新pid
    
    return is_running;
}

int GB28181Gateway::StopStreamByDevice(const std::string& device_id, const std::string& channel_id) {
    std::vector<std::pair<std::string, std::string>> streams_to_stop; // <app, stream>
    
    // 1. 收集需要停止的流
    {
        std::lock_guard<std::mutex> lock(streams_mutex_);
        for (const auto& pair : streams_) {
            const StreamInfo& info = pair.second;
            if (info.device_id == device_id) {
                // 如果指定了channel_id，则必须匹配
                if (channel_id.empty() || info.channel_id == channel_id) {
                    streams_to_stop.push_back({info.target_app, info.target_stream});
                }
            }
        }
    }
    
    // 2. 停止流（在锁外执行，因为Stop内部会加锁）
    int stopped_count = 0;
    for (const auto& pair : streams_to_stop) {
        if (Stop(pair.first, pair.second).IsSuccess()) {
            stopped_count++;
        } else {
            // 如果Stop失败，尝试使用基础名称（去掉时间戳）
            std::string base_stream = pair.second;
            size_t last_underscore = base_stream.find_last_of('_');
            if (last_underscore != std::string::npos) {
                std::string possible_timestamp = base_stream.substr(last_underscore + 1);
                bool is_timestamp = !possible_timestamp.empty() && 
                                  possible_timestamp.length() >= 10 &&
                                  std::all_of(possible_timestamp.begin(), possible_timestamp.end(), ::isdigit);
                if (is_timestamp) {
                    base_stream = base_stream.substr(0, last_underscore);
                    if (Stop(pair.first, base_stream).IsSuccess()) {
                        stopped_count++;
                    }
                }
            }
        }
    }
    
    return stopped_count;
}

GatewayStatus GB28181Gateway::GetStatus(const std::string& target_app,
                                        const std::string& target_stream) {
    std::string stream_id = GenerateStreamId(target_app, target_stream);
    
    std::lock_guard<std::mutex> lock(streams_mutex_);
    
    auto it = streams_.find(stream_id);
    if (it == streams_.end()) {
        return GatewayStatus::Stopped;
    }

    StreamInfo& info = it->second;
    
    // 使用工具类获取流状态（GB28181没有进程，pid=0）
    int dummy_pid = 0;  // GB28181不使用进程，pid固定为0
    int new_pid;
    gateway::GatewayStatus status = ::utils::StreamStatusChecker::GetStatus(
        dummy_pid,  // GB28181不使用进程，pid=0
        info.status,
        zlm_client_,
        target_app,
        target_stream,
        new_pid
    );
    
    info.status = status;
    // GB28181不使用进程，不需要更新pid
    
    return status;
}

std::vector<GB28181Device> GB28181Gateway::DiscoverDevices(int timeout_seconds) {
    // GB28181设备通过SIP REGISTER主动注册，此方法返回已注册的设备列表
    (void)timeout_seconds;  // 暂时不使用超时参数
    return ListDevices();
}

std::vector<GB28181Device> GB28181Gateway::ListDevices() const {
    std::lock_guard<std::mutex> lock(devices_mutex_);
    
    std::vector<GB28181Device> result;
    result.reserve(devices_.size());
    for (const auto& pair : devices_) {
        result.push_back(pair.second);
    }
    
    return result;
}

GB28181Device GB28181Gateway::GetDevice(const std::string& device_id) const {
    std::lock_guard<std::mutex> lock(devices_mutex_);
    auto it = devices_.find(device_id);
    if (it != devices_.end()) {
        return it->second;
    }
    return GB28181Device();  // 返回空对象
}

std::string GB28181Gateway::AddDevice(const GB28181Device& device) {
    std::lock_guard<std::mutex> lock(devices_mutex_);
    
    // 创建设备副本并设置时间戳
    GB28181Device device_copy = device;
    // 手动添加的设备不应该标记为在线，需要等待设备通过SIP REGISTER注册
    // 如果设备已经存在且在线，保持在线状态；否则标记为离线
    auto it = devices_.find(device.id);
    if (it != devices_.end() && it->second.online) {
        // 设备已存在且在线，保持在线状态（可能是更新设备信息）
        device_copy.online = true;
        device_copy.last_seen = it->second.last_seen;
        device_copy.register_time = it->second.register_time;
    } else {
        // 新设备或离线设备，标记为离线，等待SIP REGISTER注册
        device_copy.online = false;
        device_copy.register_time = std::chrono::system_clock::now();
        device_copy.last_seen = std::chrono::system_clock::now();
    }
    
    devices_[device.id] = device_copy;
    
    LOG_INFO("手动添加设备: {} ({}), 状态: {} (等待SIP REGISTER注册)", 
             device.id, device.ip.empty() ? "N/A" : device.ip, 
             device_copy.online ? "在线" : "离线");
    return device.id;
}

bool GB28181Gateway::RemoveDevice(const std::string& device_id) {
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

bool GB28181Gateway::UpdateDeviceCredentials(const std::string& device_id,
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
    LOG_INFO("更新设备认证信息: {}", device_id);
    return true;
}

streaming::RtpServerInfo GB28181Gateway::GetRtpServerInfo(const std::string& target_app,
                                                         const std::string& target_stream) const {
    std::string stream_id = GenerateStreamId(target_app, target_stream);
    std::lock_guard<std::mutex> lock(streams_mutex_);
    auto it = streams_.find(stream_id);
    if (it != streams_.end()) {
        return zlm_client_->GetRtpServerInfo(it->second.rtp_stream_id);
    }
    return streaming::RtpServerInfo();  // 返回空对象
}

std::string GB28181Gateway::GetRtpStreamId(const std::string& target_app,
                                           const std::string& target_stream) const {
    std::string stream_id = GenerateStreamId(target_app, target_stream);
    std::lock_guard<std::mutex> lock(streams_mutex_);
    
    LOG_TRACE("GB28181Gateway::GetRtpStreamId: 查找 app={}, stream={}, stream_id={}", 
              target_app, target_stream, stream_id);
    
    // 首先尝试直接查找（使用target_stream作为key）
    auto it = streams_.find(stream_id);
    if (it != streams_.end() && !it->second.rtp_stream_id.empty()) {
        LOG_TRACE("GB28181Gateway::GetRtpStreamId: 直接查找成功，rtp_stream_id={}", 
                  it->second.rtp_stream_id);
        return it->second.rtp_stream_id;
    }
    
    // 如果直接查找失败，可能是因为StreamManager中存储的stream名称和Gateway中的target_stream不同
    // 对于GB28181流，StreamManager中存储的stream可能是device_id_channel_id格式（如：34020000001320000001_34020000001320000002）
    // 而Gateway中存储的target_stream可能是用户指定的名称（如：test_final_verification）
    // 但是rtp_stream_id是device_id_channel_id_timestamp格式（如：34020000001320000001_34020000001320000002_1765374249723）
    // 所以我们需要通过target_stream（去掉时间戳）来匹配rtp_stream_id
    // 优化：先收集需要检查的流信息，然后释放锁再进行比较，减少锁持有时间
    // 对于GB28181流，需要同时查找target_app和"live" app的流（因为前端通常使用live app）
    std::vector<std::pair<std::string, std::string>> candidate_streams;  // <rtp_stream_id, target_stream>
    LOG_TRACE("GB28181Gateway::GetRtpStreamId: 直接查找失败，收集候选流信息...");
    for (const auto& pair : streams_) {
        const StreamInfo& info = pair.second;
        // 同时查找target_app和"live" app的流（因为前端通常使用live app）
        if ((info.target_app == target_app || info.target_app == "live") && !info.rtp_stream_id.empty()) {
            candidate_streams.push_back({info.rtp_stream_id, info.target_stream});
        }
    }
    // 锁在这里自动释放（lock_guard作用域结束）
    
    // 在锁外进行匹配检查，避免长时间持有锁
    for (const auto& candidate : candidate_streams) {
        const std::string& rtp_stream_id = candidate.first;
        const std::string& candidate_target_stream = candidate.second;
        
        // 检查rtp_stream_id（去掉时间戳）是否匹配target_stream
        // rtp_stream_id格式：device_id_channel_id_timestamp
        // target_stream可能是：device_id_channel_id 或 test_stream_playback
        std::string rtp_base = rtp_stream_id;
        size_t last_underscore = rtp_base.find_last_of('_');
        if (last_underscore != std::string::npos) {
            std::string rtp_without_timestamp = rtp_base.substr(0, last_underscore);
            LOG_TRACE("GB28181Gateway::GetRtpStreamId: 检查流 target_stream={}, rtp_stream_id={}, rtp_without_timestamp={}", 
                      candidate_target_stream, rtp_stream_id, rtp_without_timestamp);
            // 如果target_stream匹配rtp_stream_id（去掉时间戳），返回rtp_stream_id
            if (rtp_without_timestamp == target_stream) {
                LOG_TRACE("GB28181Gateway::GetRtpStreamId: 匹配成功（rtp_without_timestamp == target_stream），返回 rtp_stream_id={}", 
                          rtp_stream_id);
                return rtp_stream_id;
            }
        }
        // 也检查target_stream是否匹配
        if (candidate_target_stream == target_stream) {
            LOG_TRACE("GB28181Gateway::GetRtpStreamId: 匹配成功（info.target_stream == target_stream），返回 rtp_stream_id={}", 
                      rtp_stream_id);
            return rtp_stream_id;
        }
    }
    
    // 第二阶段：如果Gateway中找不到，尝试从ZLM直接查询（不持有锁，避免阻塞）
    // 这对于流信息被清理但流仍在ZLM中运行的情况很有用
    // 重要：在调用ZLM API之前已经释放锁，避免阻塞其他请求
    // 优化：只在必要时查询ZLM，避免频繁调用导致API超时
    // 如果Gateway中找不到流，说明流可能已经被清理，直接返回空字符串，不查询ZLM
    // 这样可以避免在获取流列表等高频API中频繁调用ZLM API导致超时
    // 注意：如果需要从ZLM查询，应该使用更具体的查询（如按app和schema），而不是查询所有流
    // 暂时禁用ZLM查询，因为：
    // 1. 如果Gateway中找不到，说明流已经被清理，ZLM中应该也没有了
    // 2. 频繁查询ZLM会导致API超时
    // 3. 如果需要，可以在特定场景下（如流停止时）单独查询
    /*
    if (zlm_client_) {
        try {
            LOG_TRACE("GB28181Gateway::GetRtpStreamId: Gateway中未找到流信息，尝试从ZLM直接查询...");
            // 优化：使用更具体的查询，只查询rtp schema的流，减少查询时间
            auto zlm_streams = zlm_client_->GetStreamList("rtp");  // 只查询rtp schema的流
            LOG_TRACE("GB28181Gateway::GetRtpStreamId: 从ZLM查询到{}个RTP流，查找匹配target_stream={}的流...", 
                     zlm_streams.size(), target_stream);
            
            for (const auto& zlm_stream : zlm_streams) {
                if (zlm_stream.app == target_app && 
                    streaming::ZLMClient::IsStreamActive(zlm_stream)) {
                    // 检查流名称是否匹配target_stream（去掉时间戳后）
                    std::string zlm_stream_name = zlm_stream.stream;
                    LOG_TRACE("GB28181Gateway::GetRtpStreamId: 检查ZLM流: app={}, stream={}, schema={}", 
                             zlm_stream.app, zlm_stream_name, zlm_stream.schema);
                    size_t last_underscore = zlm_stream_name.find_last_of('_');
                    if (last_underscore != std::string::npos) {
                        std::string zlm_stream_without_timestamp = zlm_stream_name.substr(0, last_underscore);
                        if (zlm_stream_without_timestamp == target_stream) {
                            LOG_TRACE("GB28181Gateway::GetRtpStreamId: 从ZLM直接查询找到匹配的流: {}", zlm_stream_name);
                            return zlm_stream_name;
                        }
                    }
                    // 也检查完全匹配（虽然不太可能，但为了兼容性）
                    if (zlm_stream_name == target_stream) {
                        LOG_TRACE("GB28181Gateway::GetRtpStreamId: 从ZLM直接查询找到完全匹配的流: {}", zlm_stream_name);
                        return zlm_stream_name;
                    }
                }
            }
            LOG_TRACE("GB28181Gateway::GetRtpStreamId: 从ZLM直接查询也未找到匹配的流");
        } catch (const std::exception& e) {
            LOG_WARN("GB28181Gateway::GetRtpStreamId: 从ZLM查询流信息时出现异常: {}", e.what());
        }
    }
    */
    
    LOG_TRACE("GB28181Gateway::GetRtpStreamId: 未找到匹配的流，返回空字符串");
    return "";  // 返回空字符串
}

// ParseSourceUrl 方法已移除，改用 utils::url_parser::ParseDeviceURL

void GB28181Gateway::OnDeviceRegister(const std::string& device_id,
                                     const std::string& ip,
                                     int port,
                                     int expires) {
    LOG_INFO("设备注册: device_id={}, ip={}, port={}, expires={}", 
            device_id, ip, port, expires);
    
    std::lock_guard<std::mutex> lock(devices_mutex_);
    auto it = devices_.find(device_id);
    if (it != devices_.end()) {
        // 更新现有设备
        it->second.ip = ip;
        it->second.port = port;
        it->second.online = true;
        it->second.last_seen = std::chrono::system_clock::now();
        if (it->second.register_time.time_since_epoch().count() == 0) {
            it->second.register_time = std::chrono::system_clock::now();
        }
    } else {
        // 添加新设备
        GB28181Device device;
        device.id = device_id;
        device.ip = ip;
        device.port = port;
        device.online = true;
        device.register_time = std::chrono::system_clock::now();
        device.last_seen = std::chrono::system_clock::now();
        devices_[device_id] = device;
    }
}

void GB28181Gateway::OnDeviceHeartbeat(const std::string& device_id) {
    std::lock_guard<std::mutex> lock(devices_mutex_);
    auto it = devices_.find(device_id);
    if (it != devices_.end()) {
        it->second.last_seen = std::chrono::system_clock::now();
        if (!it->second.online) {
            it->second.online = true;
            LOG_INFO("设备重新上线: {}", device_id);
        }
    }
}

bool GB28181Gateway::SendInvite(const GB28181Device& device,
                                const std::string& channel_id,
                                const streaming::RtpServerInfo& rtp_info,
                                std::string& call_id) {
    // 生成Call-ID
    call_id = "call_" + std::to_string(std::chrono::system_clock::now().time_since_epoch().count());
    
    // 处理Docker虚拟网络IP：如果设备IP是192.168.65.x（Docker虚拟网络），
    // 且设备使用host网络模式（监听在127.0.0.1），则使用127.0.0.1
    std::string target_ip = device.ip;
    if (target_ip.find("192.168.65.") == 0 && device.port == 5061) {
        // 可能是Docker容器使用host网络模式，尝试使用127.0.0.1
        LOG_DEBUG("GB28181 Gateway: 检测到Docker虚拟网络IP {}，尝试使用127.0.0.1", target_ip);
        target_ip = "127.0.0.1";
    }
    
    // 构造请求URI
    std::string uri = "sip:" + channel_id + "@" + target_ip + ":" + std::to_string(device.port);
    
    // 构造To
    std::string to = "<sip:" + channel_id + "@" + domain_ + ">";
    
    // 构造SDP
    std::string local_ip = GetLocalIP();
    std::string sdp = BuildSdpResponse(rtp_info, local_ip);
    
    // 构造额外的头部字段（包括Call-ID）
    std::map<std::string, std::string> headers;
    headers["Call-ID"] = call_id;  // 传递自定义Call-ID
    headers["Subject"] = channel_id + ":" + server_id_ + ",0:0";
    headers["Content-Type"] = "Application/SDP";
    
    // 验证设备IP和端口
    if (device.ip.empty() || device.port <= 0) {
        LOG_ERROR("GB28181 Gateway: 设备IP或端口无效: device_id={}, ip={}, port={}", 
                 device.id, device.ip, device.port);
        return false;
    }
    
    // 验证SDP内容
    if (sdp.empty()) {
        LOG_ERROR("GB28181 Gateway: SDP内容为空: device_id={}, channel_id={}", 
                 device.id, channel_id);
        return false;
    }
    
    // 发送INVITE请求（SipServer会使用headers中的Call-ID）
    bool success = false;
    try {
        success = sip_server_->SendRequest(
            SipMethod::INVITE,
            uri,
            to,
            headers,
            sdp,
            target_ip,
            device.port
        );
    } catch (const std::exception& e) {
        LOG_ERROR("GB28181 Gateway: 发送SIP INVITE异常: device_id={}, channel_id={}, error={}", 
                 device.id, channel_id, e.what());
        return false;
    }
    
    if (success) {
        LOG_INFO("发送SIP INVITE成功: device_id={}, channel_id={}, call_id={}, 目标地址={}:{} (原始IP: {}), RTP端口={}",
                device.id, channel_id, call_id, target_ip, device.port, device.ip, rtp_info.port);
        LOG_DEBUG("GB28181 Gateway: INVITE请求详情 - URI={}, To={}, SDP长度={}", 
                 uri, to, sdp.length());
    } else {
        LOG_ERROR("GB28181 Gateway: 发送SIP INVITE失败（可能是网络异常）: device_id={}, channel_id={}, ip={}, port={}, call_id={}", 
                 device.id, channel_id, device.ip, device.port, call_id);
    }
    
    return success;
}

bool GB28181Gateway::SendBye(const std::string& call_id,
                             const std::string& device_ip,
                             int device_port) {
    // 构造请求URI
    std::string uri = "sip:" + device_ip + ":" + std::to_string(device_port);
    
    // 构造From
    std::string from = "<sip:" + server_id_ + "@" + domain_ + ">";
    
    // 构造To
    std::string to = "<sip:" + device_ip + "@" + domain_ + ">";
    
    // 在headers中传递call_id，确保SendRequest使用正确的call_id而不是生成新的
    std::map<std::string, std::string> headers;
    headers["Call-ID"] = call_id;
    
    // 发送BYE请求
    bool success = sip_server_->SendRequest(
        SipMethod::BYE,
        uri,
        to,
        headers,  // 传递包含Call-ID的headers
        "",
        device_ip,
        device_port
    );
    
    if (success) {
        LOG_INFO("发送SIP BYE成功: call_id={}", call_id);
    } else {
        LOG_ERROR("GB28181 Gateway: 发送SIP BYE失败: call_id={}", call_id);
    }
    
    return success;
}

std::string GB28181Gateway::BuildSdpResponse(const streaming::RtpServerInfo& rtp_info,
                                            const std::string& local_ip) {
    std::ostringstream sdp;
    
    // SDP版本
    sdp << "v=0\r\n";
    
    // 会话信息
    sdp << "o=" << server_id_ << " 0 0 IN IP4 " << local_ip << "\r\n";
    sdp << "s=Play\r\n";
    sdp << "c=IN IP4 " << local_ip << "\r\n";
    sdp << "t=0 0\r\n";
    
    // 媒体描述（视频）
    sdp << "m=video " << rtp_info.port << " RTP/AVP 96\r\n";
    sdp << "a=recvonly\r\n";
    sdp << "a=rtpmap:96 PS/90000\r\n";
    
    // 媒体描述（音频，可选）
    // 注意：GB28181通常使用PS流，包含音视频
    
    return sdp.str();
}

bool GB28181Gateway::ParseSdpRequest(const std::string& sdp_body,
                                    std::string& device_ip,
                                    int& device_port) {
    // 解析SDP，提取设备的RTP接收地址
    // 简化实现：从c=行提取IP，从m=行提取端口
    if (sdp_body.empty()) {
        LOG_ERROR("GB28181 Gateway: SDP内容为空");
        return false;
    }
    
    std::istringstream stream(sdp_body);
    std::string line;
    bool found_ip = false;
    bool found_port = false;
    
    while (std::getline(stream, line)) {
        // 去除回车符
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        if (line.empty()) {
            continue;
        }
        
        // 解析c=行：c=IN IP4 192.168.1.100
        if (line.find("c=IN IP4 ") == 0) {
            device_ip = line.substr(9);
            // 去除可能的端口部分和空格
            size_t space_pos = device_ip.find(' ');
            if (space_pos != std::string::npos) {
                device_ip = device_ip.substr(0, space_pos);
            }
            // 去除可能的回车符
            if (!device_ip.empty() && device_ip.back() == '\r') {
                device_ip.pop_back();
            }
            found_ip = !device_ip.empty();
        }
        
        // 解析m=行：m=video 5060 RTP/AVP 96
        if (line.find("m=video ") == 0) {
            std::istringstream m_line(line.substr(8));
            if (m_line >> device_port) {
                found_port = device_port > 0;
            }
        }
    }
    
    if (!found_ip || !found_port) {
        LOG_ERROR("GB28181 Gateway: SDP解析失败 - IP: {}, Port: {}", 
                 found_ip ? device_ip : "未找到", found_port ? std::to_string(device_port) : "未找到");
        return false;
    }
    
    return true;
}

void GB28181Gateway::CheckDeviceHeartbeat() {
    LOG_INFO("心跳检查线程启动");
    
    while (heartbeat_check_running_) {
        std::this_thread::sleep_for(std::chrono::seconds(time::GB28181_HEARTBEAT_INTERVAL_SEC));  // 心跳检查间隔
        
        auto now = std::chrono::system_clock::now();
        std::vector<std::string> offline_devices;
        
        {
            std::lock_guard<std::mutex> lock(devices_mutex_);
            for (auto& pair : devices_) {
                auto& device = pair.second;
                if (device.online) {
                    auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(
                        now - device.last_seen).count();
                    if (elapsed > heartbeat_timeout_seconds_) {
                        device.online = false;
                        offline_devices.push_back(device.id);
                        LOG_WARN("设备心跳超时，标记为离线: {}", device.id);
                    }
                }
            }
        }
        
        // 如果启用自动清理，删除离线设备
        if (auto_remove_offline_ && !offline_devices.empty()) {
            std::lock_guard<std::mutex> lock(devices_mutex_);
            for (const auto& device_id : offline_devices) {
                devices_.erase(device_id);
                LOG_INFO("GB28181 Gateway 自动删除离线设备: {}", device_id);
            }
        }
    }
    
    LOG_INFO("GB28181 Gateway 心跳检查线程退出");
}

void GB28181Gateway::CheckStreamStatus() {
    LOG_INFO("GB28181 Gateway 流状态检查线程启动");
    
    while (stream_status_check_running_) {
        // 重要：检测间隔改为10秒，因为更推荐的方式是依赖ZLM的hook通知
        // ZLM的on_stream_changed hook会在流注册时立即通知Gateway，这是实时的
        // 轮询检测只作为兜底机制，用于处理hook通知失败或延迟的情况
        // 10秒的间隔足够长，不会对系统造成负担，同时也能及时检测到流状态变化
        std::this_thread::sleep_for(std::chrono::seconds(time::GB28181_STATUS_CHECK_INTERVAL_SEC));  // 流状态检查间隔
        
        std::vector<std::string> failed_streams;
        
        // 优化：先收集需要检查的流信息，然后释放锁，在锁外执行ZLM API调用
        std::vector<std::pair<std::string, StreamInfo>> streams_to_check;
        {
            std::lock_guard<std::mutex> lock(streams_mutex_);
            for (const auto& pair : streams_) {
                const StreamInfo& info = pair.second;
                // 只检查状态为Starting或Running的流
                if (info.status == GatewayStatus::Starting || info.status == GatewayStatus::Running) {
                    streams_to_check.push_back({pair.first, info});
                }
            }
        } // 锁在这里释放
        
        // 在锁外执行ZLM API调用，避免阻塞
        for (auto& stream_pair : streams_to_check) {
            const std::string& stream_id = stream_pair.first;
            StreamInfo& info = stream_pair.second;
            
            // 检查流是否在ZLM中存在且活跃
            // 注意：ZLM中RTP流的stream名称应该和rtp_stream_id一致（创建RTP服务器时使用的stream_id）
            // 如果找不到，可能是流还没有注册到ZLM（需要等待），或者流名称确实不一致（需要检查）
            if (zlm_client_) {
                try {
                    bool is_active = false;
                    streaming::StreamInfo stream_info;
                    
                    // 重要：更推荐的方式是依赖ZLM的hook通知，而不是主动轮询
                    // ZLM的on_stream_changed hook会在流注册时立即通知Gateway，这是实时的
                    // 轮询检测只作为兜底机制，用于处理hook通知失败或延迟的情况
                    // 因此，我们简化轮询逻辑，只做基本的检测，不进行重试
                    
                    // 优先使用rtp_stream_id检查（这是创建RTP服务器时使用的stream_id，应该和ZLM中的流名称一致）
                    if (!info.rtp_stream_id.empty()) {
                        // 先尝试使用target_app查找（因为现在RTP服务器使用target_app创建）
                        stream_info = zlm_client_->GetStreamInfo(info.target_app, info.rtp_stream_id, "rtp");
                        is_active = streaming::ZLMClient::IsStreamActive(stream_info);
                        
                        // 如果找不到，尝试使用"gb28181" app（兼容旧版本或如果target_app不是gb28181）
                        if (!is_active && info.target_app != "gb28181") {
                            stream_info = zlm_client_->GetStreamInfo("gb28181", info.rtp_stream_id, "rtp");
                            is_active = streaming::ZLMClient::IsStreamActive(stream_info);
                            if (is_active) {
                                LOG_DEBUG("GB28181 Gateway: 在gb28181 app中找到流（兼容旧版本）: {}/{}", 
                                        "gb28181", info.rtp_stream_id);
                            }
                        }
                        
                        if (is_active) {
                            LOG_DEBUG("GB28181 Gateway: 使用rtp_stream_id找到活跃流（轮询检测，兜底机制）: {}/{}", 
                                    stream_info.app, info.rtp_stream_id);
                        } else {
                            // 如果找不到，记录详细信息以便调试
                            // 注意：这可能是正常的（流还没有注册到ZLM），也可能是问题（流名称不一致）
                            // 但更可能的是hook通知已经处理了，这里只是兜底检测
                            if (info.sip_200_ok_received) {
                                // 已收到200 OK，流应该已经注册，如果找不到可能是问题
                                // 但更可能的是hook通知已经处理了，这里只是兜底检测
                                LOG_DEBUG("GB28181 Gateway: 使用rtp_stream_id未找到流（已收到200 OK）: {}/{} (可能流还未注册到ZLM，或流名称不一致，或hook通知已处理)", 
                                        info.target_app, info.rtp_stream_id);
                            } else {
                                // 未收到200 OK，流还没注册是正常的
                                LOG_DEBUG("GB28181 Gateway: 使用rtp_stream_id未找到流（未收到200 OK）: {}/{}", 
                                        info.target_app, info.rtp_stream_id);
                            }
                        }
                    } else {
                        // rtp_stream_id为空，使用target_stream作为后备（这种情况不应该发生）
                        LOG_WARN("GB28181 Gateway: rtp_stream_id为空，使用target_stream作为后备: {}/{}", 
                                info.target_app, info.target_stream);
                        stream_info = zlm_client_->GetStreamInfo(info.target_app, info.target_stream, "rtp");
                        is_active = streaming::ZLMClient::IsStreamActive(stream_info);
                    }
                    
                    if (!is_active) {
                        // 流不存在或未活跃
                        auto now = std::chrono::system_clock::now();
                        
                        if (info.status == GatewayStatus::Starting) {
                                // Starting状态的流，需要区分两种情况：
                                // 1. 未收到200 OK：等待SIP响应超时
                                // 2. 已收到200 OK但RTP流未推送：等待RTP流推送超时
                                
                                if (!info.sip_200_ok_received) {
                                    // 情况1：未收到200 OK响应
                                    auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(
                                        now - info.created_time).count();
                                    
                                    if (elapsed >= stream_start_timeout_seconds_) {
                                        // 超时：设备未响应200 OK
                                        LOG_WARN("GB28181 Gateway: 流启动超时（{}秒未收到200 OK响应）: {}/{} (Call-ID: {}, 设备: {}, 通道: {})", 
                                                elapsed, info.target_app, info.target_stream, 
                                                info.call_id.empty() ? "N/A" : info.call_id,
                                                info.device_id, info.channel_id);
                                        info.status = GatewayStatus::Error;
                                        failed_streams.push_back(stream_id);
                                        
                                        // 通知StreamManager流创建失败
                                        if (stream_manager_) {
                                            std::string error_msg = "设备未响应（超时" + std::to_string(elapsed) + "秒）";
                                            using namespace gateway::utils;
                                            auto [error_code, error_message] = InferErrorFromMessage(error_msg);
                                            stream_manager_->OnStreamCreateResult(
                                                info.target_app, info.target_stream, false,
                                                error_code, error_message);
                                        }
                                    } else {
                                        // 记录等待中的状态（每10秒记录一次，避免日志过多）
                                        if (elapsed % 10 == 0 && elapsed > 0) {
                                            LOG_DEBUG("GB28181 Gateway: 等待SIP 200 OK响应 ({}/{}秒): {}/{} (Call-ID: {})", 
                                                    elapsed, stream_start_timeout_seconds_,
                                                    info.target_app, info.target_stream,
                                                    info.call_id.empty() ? "N/A" : info.call_id);
                                        }
                                    }
                                } else {
                                    // 情况2：已收到200 OK，但RTP流还未推送到ZLM
                                    // 使用较长的超时时间（60秒）等待RTP流推送，因为设备启动ffmpeg需要时间
                                    auto elapsed_since_200ok = std::chrono::duration_cast<std::chrono::seconds>(
                                        now - info.sip_200_ok_time).count();
                                    int rtp_push_timeout = 60;  // RTP流推送超时时间（秒）- 增加到60秒，给设备更多时间启动ffmpeg
                                    
                                    if (elapsed_since_200ok >= rtp_push_timeout) {
                                        // 超时：已收到200 OK但RTP流未推送
                                        // 在清理流信息之前，先检查RTP服务器是否接收到数据（peer_ip）
                                        // 如果RTP服务器已接收到数据，说明设备正在推流，只是ZLM还没注册流，不应该关闭
                                        bool rtp_server_receiving_data = false;
                                        std::string rtp_stream_id_to_check = info.rtp_stream_id;
                                        // 注意：GetRtpInfo的app参数应该使用创建RTP服务器时使用的app
                                        // 但ZLM的getRtpInfo API可能期望使用"rtp"作为app，而不是target_app
                                        // 尝试两种app值，确保能正确查询
                                        std::string app_to_check = info.target_app.empty() ? "rtp" : info.target_app;
                                        if (!rtp_stream_id_to_check.empty() && zlm_client_) {
                                            // 在锁外执行ZLM API调用
                                            try {
                                                // 使用GetRtpInfo检查RTP服务器是否接收到数据（peer_ip有值）
                                                // 先尝试使用target_app，如果失败再尝试使用"rtp"
                                                streaming::RtpInfo rtp_info;
                                                bool rtp_info_retrieved = false;
                                                try {
                                                    rtp_info = zlm_client_->GetRtpInfo(
                                                        rtp_stream_id_to_check, 
                                                        app_to_check,
                                                        "__defaultVhost__"
                                                    );
                                                    rtp_info_retrieved = true;
                                                } catch (const std::exception& e1) {
                                                    // 如果使用target_app失败，尝试使用"rtp"
                                                    if (app_to_check != "rtp") {
                                                        try {
                                                            rtp_info = zlm_client_->GetRtpInfo(
                                                                rtp_stream_id_to_check, 
                                                                "rtp",
                                                                "__defaultVhost__"
                                                            );
                                                            rtp_info_retrieved = true;
                                                        } catch (const std::exception& e2) {
                                                            LOG_WARN("GB28181 Gateway: 查询RTP服务器状态失败（尝试了{}和rtp）: {}, {}", 
                                                                    app_to_check, e1.what(), e2.what());
                                                        }
                                                    } else {
                                                        LOG_WARN("GB28181 Gateway: 查询RTP服务器状态失败: {}", e1.what());
                                                    }
                                                }
                                                
                                                if (rtp_info_retrieved && !rtp_info.peer_ip.empty() && rtp_info.peer_ip != "0.0.0.0") {
                                                    rtp_server_receiving_data = true;
                                                    LOG_INFO("GB28181 Gateway: RTP服务器已接收到数据（peer_ip={}, peer_port={}），但ZLM流未注册，继续等待: {}", 
                                                            rtp_info.peer_ip, rtp_info.peer_port, rtp_stream_id_to_check);
                                                }
                                            } catch (const std::exception& e) {
                                                LOG_WARN("GB28181 Gateway: 查询RTP服务器状态时出现异常: {}", e.what());
                                            }
                                        }
                                        
                                        // 如果RTP服务器已接收到数据，继续等待，不关闭
                                        if (rtp_server_receiving_data) {
                                            LOG_DEBUG("GB28181 Gateway: RTP服务器已接收到数据，但ZLM流未注册，保持Starting状态继续等待: {}/{}", 
                                                    info.target_app, info.target_stream);
                                        } else {
                                            // RTP服务器未接收到数据，再次确认流确实不存在（通过查询ZLM的媒体列表）
                                            // 因为GetStreamInfo可能因为流名称不匹配而找不到流，但流实际上已经在ZLM中运行
                                            bool stream_found_in_zlm = false;
                                            std::string target_app_to_check = info.target_app;
                                            if (!rtp_stream_id_to_check.empty() && zlm_client_) {
                                                // 在锁外执行ZLM API调用
                                                try {
                                                    auto zlm_streams = zlm_client_->GetStreamList("rtp");
                                                    for (const auto& zlm_stream : zlm_streams) {
                                                        if (zlm_stream.app == target_app_to_check && 
                                                            zlm_stream.stream == rtp_stream_id_to_check &&
                                                            streaming::ZLMClient::IsStreamActive(zlm_stream)) {
                                                            stream_found_in_zlm = true;
                                                            LOG_INFO("GB28181 Gateway: 在ZLM媒体列表中找到了流（之前GetStreamInfo未找到）: {}/{}", 
                                                                    target_app_to_check, rtp_stream_id_to_check);
                                                            break;
                                                        }
                                                    }
                                                } catch (const std::exception& e) {
                                                    LOG_WARN("GB28181 Gateway: 查询ZLM媒体列表时出现异常: {}", e.what());
                                                }
                                            }
                                            
                                            if (stream_found_in_zlm) {
                                                // 流实际上在ZLM中运行，只是GetStreamInfo没有找到
                                                // 不清理流信息，继续等待hook通知或下次轮询检测
                                                LOG_DEBUG("GB28181 Gateway: 流在ZLM中运行，但GetStreamInfo未找到，保持Starting状态等待hook通知: {}/{}", 
                                                        info.target_app, info.target_stream);
                                            } else {
                                                // 确认流确实不存在且RTP服务器未接收到数据，标记为错误并清理
                                                LOG_WARN("GB28181 Gateway: RTP流推送超时（收到200 OK后{}秒仍未在ZLM中检测到流，且RTP服务器未接收到数据）: {}/{} (Call-ID: {}, 设备: {}, 通道: {})", 
                                                        elapsed_since_200ok, info.target_app, info.target_stream, 
                                                        info.call_id.empty() ? "N/A" : info.call_id,
                                                        info.device_id, info.channel_id);
                                                info.status = GatewayStatus::Error;
                                                failed_streams.push_back(stream_id);
                                                
                                                // 通知StreamManager流创建失败
                                                if (stream_manager_) {
                                                    std::string error_msg = "RTP流推送超时（收到200 OK后" + std::to_string(elapsed_since_200ok) + "秒仍未检测到流）";
                                                    using namespace gateway::utils;
                                                    auto [error_code, error_message] = InferErrorFromMessage(error_msg);
                                                    stream_manager_->OnStreamCreateResult(
                                                        info.target_app, info.target_stream, false,
                                                        error_code, error_message);
                                                }
                                            }
                                        }
                                    } else {
                                        // 记录等待RTP流推送的状态（每5秒记录一次）
                                        if (elapsed_since_200ok % 5 == 0 && elapsed_since_200ok > 0) {
                                            LOG_DEBUG("GB28181 Gateway: 已收到200 OK，等待RTP流推送到ZLM ({}/{}秒): {}/{} (Call-ID: {})", 
                                                    elapsed_since_200ok, rtp_push_timeout,
                                                    info.target_app, info.target_stream,
                                                    info.call_id.empty() ? "N/A" : info.call_id);
                                        }
                                    }
                                }
                            } else if (info.status == GatewayStatus::Running) {
                                // Running状态的流如果ZLM中不存在，可能是设备停止推流
                            info.status = GatewayStatus::Stopped;
                            LOG_WARN("GB28181 Gateway: 流已停止（设备可能停止推流）: {}/{}", 
                                    info.target_app, info.target_stream);
                        }
                    } else {
                        // 流在ZLM中活跃，更新状态为Running
                        // 注意：这里检测到流活跃，但更推荐的方式是依赖ZLM的hook通知
                        // 因为hook通知是实时的，而轮询可能有延迟
                        // 但为了兼容性，我们仍然保留这个检测逻辑作为兜底
                        if (info.status == GatewayStatus::Starting) {
                            // 计算总耗时
                            auto now = std::chrono::system_clock::now();
                            auto total_elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                                now - info.created_time).count();
                            
                            info.status = GatewayStatus::Running;
                            
                            // 记录成功信息
                            if (info.sip_200_ok_received) {
                                auto rtp_elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                                    now - info.sip_200_ok_time).count();
                                LOG_INFO("GB28181 Gateway: 流状态验证成功（通过轮询检测，兜底机制），流已完全启动: {}/{} (总耗时: {}ms, 收到200 OK后RTP推送耗时: {}ms)", 
                                        info.target_app, info.target_stream, total_elapsed, rtp_elapsed);
                            } else {
                                LOG_INFO("GB28181 Gateway: 流状态验证成功（通过轮询检测，兜底机制），流已完全启动: {}/{} (总耗时: {}ms)", 
                                        info.target_app, info.target_stream, total_elapsed);
                            }
                            
                            // 通过StreamManager统一记录"创建结果"（成功）
                            // 注意：StreamManager的状态可能已经通过hook通知更新为Running
                            // 这里再次调用是为了确保状态一致
                            if (stream_manager_) {
                                GatewayBase::RegisterStreamToManager(
                                    stream_manager_,
                                    info.target_app,
                                    info.target_stream,
                                    "gb28181",
                                    info.output_protocol,
                                    info.source_url,
                                    "gb28181_gateway",
                                    GatewayStatus::Running,
                                    0,  // 不使用FFmpeg进程
                                    info.device_id,
                                    "gb28181"
                                );
                                
                                stream_manager_->OnStreamCreateResult(info.target_app, info.target_stream, true);
                            }
                        }
                    }
                    
                    // 更新流状态（需要重新加锁）
                    {
                        std::lock_guard<std::mutex> lock(streams_mutex_);
                        auto it = streams_.find(stream_id);
                        if (it != streams_.end()) {
                            it->second = info;  // 更新流信息
                        }
                    }
                } catch (const std::exception& e) {
                    LOG_ERROR("GB28181 Gateway: 检查流状态异常: {}/{} - {}", 
                            info.target_app, info.target_stream, e.what());
                }
            }
        }
        
        // 清理失败的流
        if (!failed_streams.empty()) {
            std::lock_guard<std::mutex> lock(streams_mutex_);
            for (const auto& stream_id : failed_streams) {
                auto it = streams_.find(stream_id);
                if (it != streams_.end()) {
                    // 关闭RTP服务器
                    if (zlm_client_ && !it->second.rtp_stream_id.empty()) {
                        try {
                            zlm_client_->CloseRtpServer(it->second.rtp_stream_id);
                            LOG_INFO("GB28181 Gateway 已关闭失败的RTP服务器: {}", it->second.rtp_stream_id);
                        } catch (const std::exception& e) {
                            LOG_ERROR("GB28181 Gateway: 关闭RTP服务器失败: {} - {}", 
                                    it->second.rtp_stream_id, e.what());
                        }
                    }
                    streams_.erase(it);
                    LOG_INFO("GB28181 Gateway 已清理失败的流: {}", stream_id);
                }
            }
        }
    }
    
    LOG_INFO("GB28181 Gateway 流状态检查线程退出");
}

std::string GB28181Gateway::GetLocalIP() const {
    // 如果SIP服务器绑定了特定IP，使用该IP
    if (sip_server_) {
        std::string sip_ip = sip_server_->GetLocalIP();
        if (sip_ip != "0.0.0.0" && sip_ip != "127.0.0.1" && !sip_ip.empty()) {
            return sip_ip;
        }
    }
    
    // 尝试获取默认路由的网络接口IP
    // 方法：创建一个UDP socket连接到外部地址，然后获取本地地址
    int sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock >= 0) {
        struct sockaddr_in addr;
        memset(&addr, 0, sizeof(addr));
        addr.sin_family = AF_INET;
        addr.sin_port = htons(80);
        // 使用一个公共DNS服务器地址（不会真正连接）
        if (inet_aton("8.8.8.8", &addr.sin_addr) == 1) {
            if (connect(sock, (struct sockaddr*)&addr, sizeof(addr)) == 0) {
                struct sockaddr_in local_addr;
                socklen_t len = sizeof(local_addr);
                if (getsockname(sock, (struct sockaddr*)&local_addr, &len) == 0) {
                    char ip_str[INET_ADDRSTRLEN];
                    if (inet_ntop(AF_INET, &local_addr.sin_addr, ip_str, INET_ADDRSTRLEN) != nullptr) {
                        close(sock);
                        std::string result(ip_str);
                        // 排除回环地址
                        if (result != "127.0.0.1" && result != "::1") {
                            return result;
                        }
                    }
                }
            }
        }
        close(sock);
    }
    
    // 回退到127.0.0.1
    LOG_WARN("GB28181 Gateway: 无法自动检测本地IP，使用127.0.0.1");
    return "127.0.0.1";
}

void GB28181Gateway::OnSipResponse(const std::string& call_id, int status_code, const std::string& reason_phrase, int peer_port) {
    LOG_INFO("GB28181 Gateway: 收到SIP响应: Call-ID={}, status={} {}", call_id, status_code, reason_phrase);
    
    // 通过call_id查找对应的流
    // 优化：先收集候选流信息，然后释放锁再进行比较，减少锁持有时间
    std::vector<std::pair<std::string, std::string>> candidate_streams;  // <stream_id, call_id>
    {
        std::lock_guard<std::mutex> lock(streams_mutex_);
        for (const auto& pair : streams_) {
            if (!pair.second.call_id.empty()) {
                candidate_streams.push_back({pair.first, pair.second.call_id});
            }
        }
    }
    
    std::string stream_id;
    for (const auto& candidate : candidate_streams) {
        if (candidate.second == call_id) {
            stream_id = candidate.first;
            LOG_DEBUG("GB28181 Gateway: 找到匹配的流: stream_id={}, Call-ID={}", 
                     stream_id, call_id);
            break;
        }
    }
    
    if (stream_id.empty()) {
        LOG_WARN("GB28181 Gateway: 收到响应但未找到对应的流: Call-ID={}, status={} {} (可能Call-ID不匹配或流已清理)", 
                call_id, status_code, reason_phrase);
        // 列出所有当前流的Call-ID用于调试
        {
            std::lock_guard<std::mutex> lock(streams_mutex_);
            if (!streams_.empty()) {
                LOG_DEBUG("GB28181 Gateway: 当前活跃流的Call-ID列表:");
                for (const auto& pair : streams_) {
                    LOG_DEBUG("  - stream_id={}, Call-ID={}, status={}", 
                             pair.first, 
                             pair.second.call_id.empty() ? "N/A" : pair.second.call_id,
                             pair.second.status == GatewayStatus::Starting ? "Starting" : 
                             pair.second.status == GatewayStatus::Running ? "Running" : "Other");
                }
            }
        }
        return;
    }
    
    std::lock_guard<std::mutex> lock(streams_mutex_);
    auto it = streams_.find(stream_id);
    if (it == streams_.end()) {
        return;
    }
    
    StreamInfo& info = it->second;
    
    if (status_code == 200) {
        // 200 OK响应：SIP信令成功，但RTP流可能还未推送到ZLM
        // 保持Starting状态，等待RTP流在ZLM中真正注册并活跃
        if (info.status == GatewayStatus::Starting) {
            // 计算从发送INVITE到收到200 OK的耗时
            auto now = std::chrono::system_clock::now();
            auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                now - info.created_time).count();
            
            // 重要：从200 OK响应的peer_port提取设备实际监听的端口
            // 因为200 OK响应是从设备发送回来的，peer_port就是设备实际监听的端口
            // 这比从device.port获取更准确，因为device.port可能是配置中的端口，而不是实际端口
            if (peer_port > 0) {
                info.device_sip_port = peer_port;
                LOG_DEBUG("GB28181 Gateway: 从200 OK响应提取设备端口: {} (Call-ID: {})", 
                         peer_port, call_id);
            }
            
            // 记录已收到200 OK，但保持Starting状态，等待RTP流推送
            info.sip_200_ok_received = true;
            info.sip_200_ok_time = now;
            
            LOG_INFO("GB28181 Gateway: 收到200 OK响应（SIP信令成功）: {}/{} (耗时: {}ms, Call-ID: {}, 设备端口: {}), 等待RTP流推送到ZLM...", 
                    info.target_app, info.target_stream, elapsed, call_id, 
                    info.device_sip_port > 0 ? std::to_string(info.device_sip_port) : "未提取");
            
            // 重要：不立即将状态设为Running，等待ZLM的hook通知或轮询检测到RTP流活跃
            // 更推荐的方式是依赖ZLM的on_stream_changed hook通知，因为hook通知是实时的
            // 当ZLM检测到RTP流注册时，会调用on_stream_changed hook，StreamManager会收到通知
            // 如果流已经在StreamManager中注册（状态为Starting），StreamManager会自动将状态更新为Running
            // 这样GB28181 Gateway就不需要主动轮询GetStreamInfo了，轮询只作为兜底机制
        }
    } else {
        // 其他状态码（如486 Busy Here、488 Not Acceptable等），标记为错误
        LOG_WARN("GB28181 Gateway: 收到错误响应: Call-ID={}, status={} {}, stream={}/{}", 
                call_id, status_code, reason_phrase, info.target_app, info.target_stream);
        
        info.status = GatewayStatus::Error;
        
        // 关闭RTP服务器
        if (!info.rtp_stream_id.empty() && zlm_client_) {
            try {
                zlm_client_->CloseRtpServer(info.rtp_stream_id);
            } catch (const std::exception& e) {
                LOG_WARN("GB28181 Gateway: 关闭RTP服务器失败: {} - {}", info.rtp_stream_id, e.what());
            }
        }
        
        // 通过StreamManager统一记录"创建结果"（失败）
        if (stream_manager_) {
            std::string error_msg = "设备响应错误: " + std::to_string(status_code) + " " + reason_phrase;
            using namespace gateway::utils;
            auto [error_code, error_message] = InferErrorFromMessage(error_msg);
            stream_manager_->OnStreamCreateResult(info.target_app, info.target_stream, false,
                                                 error_code, error_message);
        }
    }
}

bool GB28181Gateway::OnByeRequest(const std::string& call_id) {
    // 通过call_id查找对应的流
    // 优化：先收集候选流信息，然后释放锁再进行比较，减少锁持有时间
    std::vector<std::pair<std::string, StreamInfo>> candidate_streams;  // <stream_id, StreamInfo>
    {
        std::lock_guard<std::mutex> lock(streams_mutex_);
        for (const auto& pair : streams_) {
            if (!pair.second.call_id.empty()) {
                candidate_streams.push_back({pair.first, pair.second});
            }
        }
    }
    
    std::string stream_id;
    StreamInfo info;
    for (const auto& candidate : candidate_streams) {
        if (candidate.second.call_id == call_id) {
            stream_id = candidate.first;
            info = candidate.second;  // 复制流信息
            break;
        }
    }
    
    if (stream_id.empty()) {
        LOG_WARN("GB28181 Gateway: 收到BYE请求但未找到对应的流: Call-ID={}", call_id);
        return false;
    }
    
    // 关闭RTP服务器
    if (!info.rtp_stream_id.empty() && zlm_client_) {
        try {
            zlm_client_->CloseRtpServer(info.rtp_stream_id);
        } catch (const std::exception& e) {
            LOG_WARN("GB28181 Gateway: 关闭RTP服务器失败: {} - {}", info.rtp_stream_id, e.what());
        }
    }
    
    // 从StreamManager注销流
    if (stream_manager_) {
        stream_manager_->UnregisterStream(info.target_app, info.target_stream);
    }
    
    // 从流列表中删除
    {
        std::lock_guard<std::mutex> lock(streams_mutex_);
        streams_.erase(stream_id);
    }
    
    LOG_INFO("GB28181 Gateway: BYE请求处理成功，已停止流: {}/{}", info.target_app, info.target_stream);
    return true;
}

} // namespace gateway

