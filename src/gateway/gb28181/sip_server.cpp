#include "sip_server.hpp"
#include "sip_message.hpp"
#include "utils/logger.hpp"
#include "utils/system_utils.hpp"
#include "streaming/zlmediakit/zlm_client.hpp"
#include "config/constants.hpp"
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <cstring>
#include <algorithm>
#include <sstream>
#include <chrono>

using namespace config::constants;

namespace gateway {

SipServer::SipServer(
    const std::string& local_ip,
    int local_port,
    const std::string& server_id,
    const std::string& domain)
    : local_ip_(local_ip)
    , local_port_(local_port)
    , server_id_(server_id)
    , domain_(domain)
    , is_running_(false)
    , total_messages_received_(0)
    , total_messages_sent_(0)
    , total_errors_(0) {
}

SipServer::~SipServer() {
    Stop();
}

bool SipServer::Start() {
    if (is_running_) {
        LOG_WARN("SipServer: 服务器已在运行");
        return true;
    }
    
    // 检查端口占用并尝试清理（防止重启失败）
    ::utils::SystemUtils::CheckAndCleanPort(local_port_);
    
    // 创建UDP socket
    socket_fd_ = socket(AF_INET, SOCK_DGRAM, 0);
    if (socket_fd_ < 0) {
        LOG_ERROR("SipServer: 创建socket失败: {}", strerror(errno));
        return false;
    }
    
    // 设置socket选项
    int opt = 1;
    if (setsockopt(socket_fd_, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
        LOG_ERROR("SipServer: 设置SO_REUSEADDR失败: {}", strerror(errno));
        close(socket_fd_);
        socket_fd_ = -1;
        return false;
    }
    
    // 绑定地址
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(local_port_);
    
    if (local_ip_ == "0.0.0.0" || local_ip_.empty()) {
        addr.sin_addr.s_addr = INADDR_ANY;
    } else {
        if (inet_aton(local_ip_.c_str(), &addr.sin_addr) == 0) {
            LOG_ERROR("SipServer: 无效的绑定地址: {}", local_ip_);
            close(socket_fd_);
            socket_fd_ = -1;
            return false;
        }
    }
    
    if (bind(socket_fd_, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        LOG_ERROR("SipServer: 绑定地址失败: {}:{} - {}", 
                 local_ip_, local_port_, strerror(errno));
        close(socket_fd_);
        socket_fd_ = -1;
        return false;
    }
    
    // 设置非阻塞模式
    int flags = fcntl(socket_fd_, F_GETFL, 0);
    if (flags < 0 || fcntl(socket_fd_, F_SETFL, flags | O_NONBLOCK) < 0) {
        LOG_WARN("SipServer: 设置非阻塞模式失败: {}", strerror(errno));
    }
    
    is_running_ = true;
    
    LOG_INFO("SipServer: 启动SIP服务器成功，绑定地址: {}:{}, socket_fd={}", local_ip_, local_port_, socket_fd_);
    
    // 启动接收线程
    server_thread_ = std::thread(&SipServer::ServerLoop, this);
    
    // 启动消息处理线程
    message_thread_ = std::thread(&SipServer::MessageLoop, this);
    
    LOG_INFO("SipServer: SIP服务器线程已启动");
    
    return true;
}

void SipServer::Stop() {
    if (!is_running_) {
        return;
    }
    
    is_running_ = false;
    
    // 通知消息处理线程退出
    queue_cv_.notify_all();
    
    // 关闭socket（会中断recvfrom）
    if (socket_fd_ >= 0) {
        close(socket_fd_);
        socket_fd_ = -1;
    }
    
    // 等待线程退出
    if (server_thread_.joinable()) {
        server_thread_.join();
    }
    if (message_thread_.joinable()) {
        message_thread_.join();
    }
    
    LOG_INFO("SipServer 已停止");
}

void SipServer::ServerLoop() {
    char buffer[4096];
    struct sockaddr_in client_addr;
    socklen_t client_addr_len = sizeof(client_addr);
    
    LOG_INFO("SipServer 接收线程启动");
    
    while (is_running_) {
        ssize_t received = recvfrom(
            socket_fd_,
            buffer,
            sizeof(buffer) - 1,
            0,
            (struct sockaddr*)&client_addr,
            &client_addr_len
        );
        
        if (received < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                // 非阻塞模式，没有数据可读，继续循环
                std::this_thread::sleep_for(std::chrono::milliseconds(time::SIP_POLL_INTERVAL_MS));
                continue;
            } else if (errno == EBADF) {
                // Socket已关闭，退出循环
                break;
            } else {
                LOG_ERROR("SipServer: 接收数据失败: {}", strerror(errno));
                total_errors_++;
                std::this_thread::sleep_for(std::chrono::milliseconds(time::SIP_WAIT_MS));
                continue;
            }
        }
        
        if (received == 0) {
            continue;
        }
        
        buffer[received] = '\0';
        std::string message(buffer, received);
        
        // 解析SIP消息
        SipMessage msg = SipMessageParser::Parse(message);
        if (msg.method == SipMethod::UNKNOWN && msg.status_code == 0) {
            LOG_WARN("SipServer: 无法解析SIP消息，长度: {}", received);
            total_errors_++;
            continue;
        }
        
        // 获取客户端地址
        std::string peer_ip = inet_ntoa(client_addr.sin_addr);
        int peer_port = ntohs(client_addr.sin_port);
        
        total_messages_received_++;
        
        // 放入消息队列
        {
            std::lock_guard<std::mutex> lock(queue_mutex_);
            message_queue_.push({msg, peer_ip, peer_port});
        }
        queue_cv_.notify_one();
    }
    
    LOG_INFO("SipServer 接收线程退出");
}

void SipServer::MessageLoop() {
    LOG_INFO("SipServer 消息处理线程启动");
    
    while (is_running_ || !message_queue_.empty()) {
        MessageItem item;
        
        {
            std::unique_lock<std::mutex> lock(queue_mutex_);
            queue_cv_.wait(lock, [this] {
                return !message_queue_.empty() || !is_running_;
            });
            
            if (message_queue_.empty()) {
                continue;
            }
            
            item = message_queue_.front();
            message_queue_.pop();
        }
        
        // 处理消息
        HandleMessage(item.msg, item.peer_ip, item.peer_port);
        
        // 定期清理过期事务
        static auto last_cleanup = std::chrono::system_clock::now();
        auto now = std::chrono::system_clock::now();
        if (std::chrono::duration_cast<std::chrono::seconds>(now - last_cleanup).count() > 60) {
            CleanupExpiredTransactions();
            last_cleanup = now;
        }
    }
    
    LOG_INFO("SipServer 消息处理线程退出");
}

void SipServer::HandleMessage(const SipMessage& msg, const std::string& peer_ip, int peer_port) {
    try {
        // 检查是否是重传
        if (msg.IsRequest() && HandleRetransmission(msg, peer_ip, peer_port)) {
            LOG_DEBUG("SipServer 处理重传消息: Call-ID={}, CSeq={}", 
                     msg.call_id, msg.cseq);
            return;
        }
        
        // 根据消息类型处理
        if (msg.IsRequest()) {
            switch (msg.method) {
                case SipMethod::REGISTER:
                    HandleRegister(msg, peer_ip, peer_port);
                    break;
                case SipMethod::MESSAGE:
                    HandleMessageRequest(msg, peer_ip, peer_port);
                    break;
                case SipMethod::INVITE:
                    HandleInvite(msg, peer_ip, peer_port);
                    break;
                case SipMethod::BYE:
                    HandleBye(msg, peer_ip, peer_port);
                    break;
                case SipMethod::ACK:
                    // ACK不需要响应
                    LOG_DEBUG("SipServer 收到ACK: Call-ID={}", msg.call_id);
                    break;
                default:
                    LOG_WARN("SipServer: 未支持的方法: {}", 
                            SipMessageParser::MethodToString(msg.method));
                    SendResponse(msg, 405, "Method Not Allowed", {}, "", peer_ip, peer_port);
                    break;
            }
        } else {
            // 响应消息（处理设备对INVITE请求的响应）
            LOG_INFO("SipServer 收到响应消息: status={} {}, Call-ID={}, From={}, To={}", 
                    msg.status_code, msg.reason_phrase, msg.call_id, msg.from, msg.to);
            
            if (response_callback_) {
                if (!msg.call_id.empty()) {
                    LOG_DEBUG("SipServer: 调用响应回调: Call-ID={}, status={} {}, peer_port={}", 
                            msg.call_id, msg.status_code, msg.reason_phrase, peer_port);
                    response_callback_(msg.call_id, msg.status_code, msg.reason_phrase, peer_port);
                } else {
                    LOG_WARN("SipServer: 收到响应但Call-ID为空，无法调用回调: status={} {}", 
                            msg.status_code, msg.reason_phrase);
                }
            } else {
                LOG_WARN("SipServer: 收到响应但响应回调未设置: status={} {}, Call-ID={}", 
                        msg.status_code, msg.reason_phrase, msg.call_id);
            }
        }
    } catch (const std::exception& e) {
        LOG_ERROR("SipServer: 处理消息异常: {}", e.what());
        total_errors_++;
    }
}

void SipServer::HandleRegister(const SipMessage& request, const std::string& peer_ip, int peer_port) {
    LOG_INFO("SipServer 收到REGISTER请求: From={}, Call-ID={}", 
            request.from, request.call_id);
    
    // 提取设备ID
    std::string device_id = SipMessageParser::ExtractDeviceId(request.from);
    if (device_id.empty()) {
        LOG_ERROR("SipServer: 无法从From头部提取设备ID: {}", request.from);
        SendResponse(request, 400, "Bad Request", {}, "", peer_ip, peer_port);
        return;
    }
    
    // 检查是否需要认证
    std::string auth_header = request.GetHeader("Authorization");
    bool needs_auth = false;
    std::string username, password;
    
    if (auth_callback_) {
        needs_auth = auth_callback_(device_id, username, password);
    }
    
    // 如果需要认证但请求中没有Authorization头部，返回401 Unauthorized
    if (needs_auth && auth_header.empty()) {
        LOG_DEBUG("SipServer 设备需要认证但未提供Authorization头部，返回401: device_id={}", device_id);
        
        // 生成nonce（使用时间戳和随机数）
        auto now = std::chrono::system_clock::now();
        auto timestamp = std::chrono::duration_cast<std::chrono::seconds>(now.time_since_epoch()).count();
        std::string nonce = SipMessageParser::CalculateMD5(
            server_id_ + ":" + std::to_string(timestamp) + ":" + device_id);
        
        // 构造WWW-Authenticate头部
        std::string www_auth = "Digest realm=\"" + domain_ + "\", nonce=\"" + nonce + "\", algorithm=MD5";
        
        std::map<std::string, std::string> headers;
        headers["WWW-Authenticate"] = www_auth;
        
        SendResponse(request, 401, "Unauthorized", headers, "", peer_ip, peer_port);
        return;
    }
    
    // 如果需要认证且提供了Authorization头部，验证认证信息
    if (needs_auth && !auth_header.empty()) {
        std::string auth_username, auth_realm, auth_nonce, auth_uri, auth_response, auth_algorithm;
        
        if (!SipMessageParser::ParseAuthorizationHeader(
                auth_header, auth_username, auth_realm, auth_nonce, auth_uri, auth_response, auth_algorithm)) {
            LOG_WARN("SipServer: 无法解析Authorization头部: device_id={}", device_id);
            SendResponse(request, 400, "Bad Request", {}, "", peer_ip, peer_port);
            return;
        }
        
        // 验证认证信息
        std::string method = "REGISTER";
        if (!SipMessageParser::VerifyDigestAuth(
                username, password, auth_realm, method, auth_uri, auth_nonce, auth_response)) {
            LOG_WARN("SipServer: 设备认证失败: device_id={}, username={}", device_id, auth_username);
            SendResponse(request, 403, "Forbidden", {}, "", peer_ip, peer_port);
            return;
        }
        
        LOG_DEBUG("SipServer 设备认证成功: device_id={}, username={}", device_id, auth_username);
    }
    
    // 提取Contact信息（IP和端口）
    std::string device_ip = peer_ip;  // 默认使用peer_ip
    int device_port = peer_port;      // 默认使用peer_port
    
    std::string contact = request.contact;
    if (!contact.empty()) {
        std::string contact_ip;
        int contact_port = 0;
        if (SipMessageParser::ExtractContactInfo(contact, contact_ip, contact_port)) {
            device_ip = contact_ip;
            device_port = contact_port;
        }
    }
    
    // 提取过期时间
    int expires = request.expires;
    if (expires <= 0) {
        expires = 3600;  // 默认1小时
    }
    
    // 发送200 OK响应
    std::map<std::string, std::string> headers;
    headers["Contact"] = request.contact.empty() ? 
        ("<sip:" + device_id + "@" + domain_ + ":" + std::to_string(device_port) + ">") : 
        request.contact;
    headers["Expires"] = std::to_string(expires);
    
    bool success = SendResponse(request, 200, "OK", headers, "", peer_ip, peer_port);
    if (success) {
        LOG_INFO("SipServer 设备注册成功: device_id={}, ip={}, port={}, expires={}", 
                device_id, device_ip, device_port, expires);
        
        // 调用注册回调
        if (register_callback_) {
            register_callback_(device_id, device_ip, device_port, expires);
        }
    } else {
        LOG_ERROR("SipServer: 发送REGISTER响应失败");
    }
}

void SipServer::HandleMessageRequest(const SipMessage& request, const std::string& peer_ip, int peer_port) {
    LOG_DEBUG("SipServer 收到MESSAGE请求（心跳）: From={}, Call-ID={}", 
             request.from, request.call_id);
    
    // 提取设备ID
    std::string device_id = SipMessageParser::ExtractDeviceId(request.from);
    if (device_id.empty()) {
        LOG_WARN("SipServer: 无法从From头部提取设备ID: {}", request.from);
        SendResponse(request, 400, "Bad Request", {}, "", peer_ip, peer_port);
        return;
    }
    
    // 发送200 OK响应（快速响应心跳）
    bool success = SendResponse(request, 200, "OK", {}, "", peer_ip, peer_port);
    if (success) {
        LOG_DEBUG("SipServer 心跳响应成功: device_id={}", device_id);
        
        // 调用心跳回调
        if (heartbeat_callback_) {
            heartbeat_callback_(device_id);
        }
    } else {
        LOG_ERROR("SipServer: 发送MESSAGE响应失败");
    }
}

void SipServer::HandleInvite(const SipMessage& request, const std::string& peer_ip, int peer_port) {
    LOG_INFO("SipServer 收到INVITE请求: From={}, Call-ID={}", 
            request.from, request.call_id);
    
    // 对于INVITE请求，先发送100 Trying防止重传
    SendResponse(request, 100, "Trying", {}, "", peer_ip, peer_port);
    
    // 如果没有设置回调，直接返回488 Not Acceptable
    if (!invite_callback_) {
        LOG_WARN("SipServer: INVITE回调未设置，拒绝请求");
        SendResponse(request, 488, "Not Acceptable Here", {}, "", peer_ip, peer_port);
        return;
    }
    
    // 从From头部提取设备ID
    std::string device_id = SipMessageParser::ExtractDeviceId(request.from);
    if (device_id.empty()) {
        LOG_ERROR("SipServer: 无法从INVITE请求中提取设备ID");
        SendResponse(request, 400, "Bad Request", {}, "", peer_ip, peer_port);
        return;
    }
    
    // 从Request-URI提取通道ID
    std::string channel_id;
    size_t sip_pos = request.uri.find("sip:");
    if (sip_pos != std::string::npos) {
        size_t start = sip_pos + 4;
        size_t at_pos = request.uri.find('@', start);
        if (at_pos != std::string::npos) {
            channel_id = request.uri.substr(start, at_pos - start);
        }
    }
    if (channel_id.empty()) {
        channel_id = device_id;  // 如果没有通道ID，使用设备ID
    }
    
    // 调用回调函数处理INVITE请求
    std::string rtp_stream_id;
    streaming::RtpServerInfo rtp_info;
    bool success = invite_callback_(
        device_id,
        channel_id,
        request.call_id,
        request.body,
        peer_ip,
        peer_port,
        rtp_stream_id,
        rtp_info
    );
    
    if (success && rtp_info.port > 0) {
        // 构造SDP响应
        std::string local_ip = GetLocalIP();
        std::ostringstream sdp;
        sdp << "v=0\r\n";
        sdp << "o=" << server_id_ << " 0 0 IN IP4 " << local_ip << "\r\n";
        sdp << "s=Play\r\n";
        sdp << "c=IN IP4 " << local_ip << "\r\n";
        sdp << "t=0 0\r\n";
        sdp << "m=video " << rtp_info.port << " RTP/AVP 96\r\n";
        sdp << "a=recvonly\r\n";
        sdp << "a=rtpmap:96 PS/90000\r\n";
        
        // 发送200 OK响应
        std::map<std::string, std::string> headers;
        headers["Content-Type"] = "Application/SDP";
        SendResponse(request, 200, "OK", headers, sdp.str(), peer_ip, peer_port);
        LOG_INFO("SipServer: INVITE处理成功，已创建RTP服务器: stream_id={}, port={}", 
                rtp_stream_id, rtp_info.port);
    } else {
        // 处理失败，发送500 Internal Server Error
        LOG_ERROR("SipServer: INVITE回调处理失败");
        SendResponse(request, 500, "Internal Server Error", {}, "", peer_ip, peer_port);
    }
}

void SipServer::HandleBye(const SipMessage& request, const std::string& peer_ip, int peer_port) {
    LOG_INFO("SipServer 收到BYE请求: From={}, Call-ID={}", 
            request.from, request.call_id);
    
    // 发送200 OK响应
    SendResponse(request, 200, "OK", {}, "", peer_ip, peer_port);
    
    // 调用BYE回调处理逻辑
    if (bye_callback_ && !request.call_id.empty()) {
        // Gateway层会通过call_id查找对应的流并处理
        // rtp_stream_id参数在Gateway层内部查找，这里传入空字符串
        std::string rtp_stream_id;
        if (bye_callback_(request.call_id, rtp_stream_id)) {
            LOG_INFO("SipServer: BYE处理成功: Call-ID={}", request.call_id);
        } else {
            LOG_WARN("SipServer: BYE处理失败: Call-ID={}", request.call_id);
        }
    }
}

bool SipServer::HandleRetransmission(const SipMessage& msg, const std::string& peer_ip, int peer_port) {
    if (!msg.IsRequest() || msg.call_id.empty() || msg.cseq == 0) {
        return false;
    }
    
    std::string transaction_key = msg.call_id + ":" + std::to_string(msg.cseq);
    
    std::lock_guard<std::mutex> lock(transactions_mutex_);
    auto it = transactions_.find(transaction_key);
    if (it != transactions_.end()) {
        // 找到相同的事务，这是重传，直接重发响应
        LOG_DEBUG("SipServer 检测到重传，重发响应: {}", transaction_key);
        
        // 使用保存的对端地址重发响应
        ssize_t sent = sendto(socket_fd_, it->second.response.c_str(), it->second.response.length(), 0,
                             (struct sockaddr*)&it->second.peer_addr, sizeof(it->second.peer_addr));
        if (sent > 0) {
            total_messages_sent_++;
        }
        return true;
    }
    
    return false;
}

void SipServer::CleanupExpiredTransactions() {
    auto now = std::chrono::system_clock::now();
    std::lock_guard<std::mutex> lock(transactions_mutex_);
    
    auto it = transactions_.begin();
    while (it != transactions_.end()) {
        auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(
            now - it->second.created_time).count();
        if (elapsed > 30) {  // 30秒后清理
            it = transactions_.erase(it);
        } else {
            ++it;
        }
    }
}

std::string SipServer::GetLocalIP() const {
    if (local_ip_ != "0.0.0.0" && !local_ip_.empty()) {
        return local_ip_;
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
    LOG_WARN("SipServer: 无法自动检测本地IP，使用127.0.0.1");
    return "127.0.0.1";
}

bool SipServer::SendResponse(
    const SipMessage& request,
    int status_code,
    const std::string& reason_phrase,
    const std::map<std::string, std::string>& headers,
    const std::string& body,
    const std::string& peer_ip,
    int peer_port) {
    
    // 构造响应
    std::string response = SipMessageParser::BuildResponse(
        request, status_code, reason_phrase, headers, body);
    
    // 发送响应
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(peer_port);
    if (inet_aton(peer_ip.c_str(), &addr.sin_addr) == 0) {
        LOG_ERROR("SipServer: 无效的对端IP: {}", peer_ip);
        return false;
    }
    
    ssize_t sent = sendto(socket_fd_, response.c_str(), response.length(), 0,
                         (struct sockaddr*)&addr, sizeof(addr));
    if (sent < 0) {
        LOG_ERROR("SipServer: 发送响应失败: {}", strerror(errno));
        total_errors_++;
        return false;
    }
    
    total_messages_sent_++;
    
    // 保存事务（用于重传处理）
    if (request.IsRequest() && !request.call_id.empty() && request.cseq > 0) {
        std::string transaction_key = request.call_id + ":" + std::to_string(request.cseq);
        SipTransaction transaction;
        transaction.call_id = request.call_id;
        transaction.cseq = request.cseq;
        transaction.method = SipMessageParser::MethodToString(request.method);
        transaction.response = response;
        transaction.created_time = std::chrono::system_clock::now();
        transaction.peer_ip = peer_ip;
        transaction.peer_port = peer_port;
        memcpy(&transaction.peer_addr, &addr, sizeof(addr));
        
        std::lock_guard<std::mutex> lock(transactions_mutex_);
        transactions_[transaction_key] = transaction;
    }
    
    return true;
}

bool SipServer::SendRequest(
    SipMethod method,
    const std::string& uri,
    const std::string& to,
    const std::map<std::string, std::string>& headers,
    const std::string& body,
    const std::string& peer_ip,
    int peer_port) {
    
    // 生成Call-ID（如果headers中已有Call-ID，使用它）
    std::string call_id;
    auto call_id_it = headers.find("Call-ID");
    if (call_id_it != headers.end()) {
        call_id = call_id_it->second;
    } else {
        call_id = "call_" + std::to_string(std::chrono::system_clock::now().time_since_epoch().count());
    }
    int cseq = 1;
    
    // 构造From（使用服务器ID）
    std::string from = "<sip:" + server_id_ + "@" + domain_ + ">";
    
    // 获取本地IP和端口用于Via头部
    std::string local_ip = GetLocalIP();
    int local_port = local_port_;
    
    // 生成branch参数（用于事务标识）
    auto now = std::chrono::system_clock::now();
    auto timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count();
    std::string branch = "z9hG4bK" + std::to_string(timestamp);
    
    // 构造Via头部（SIP协议要求请求必须包含Via头部，用于响应路由）
    std::string via = "SIP/2.0/UDP " + local_ip + ":" + std::to_string(local_port) + ";branch=" + branch;
    
    // 复制headers，但移除Call-ID和Via（因为我们要添加正确的Via）
    std::map<std::string, std::string> request_headers = headers;
    request_headers.erase("Call-ID");
    request_headers["Via"] = via;  // 添加Via头部
    
    // 构造请求
    std::string request = SipMessageParser::BuildRequest(
        method, uri, from, to, call_id, cseq, request_headers, body);
    
    // 发送请求
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(peer_port);
    if (inet_aton(peer_ip.c_str(), &addr.sin_addr) == 0) {
        LOG_ERROR("SipServer: 无效的对端IP: {}", peer_ip);
        return false;
    }
    
    // 记录发送的INVITE请求详情（用于调试）
    if (method == SipMethod::INVITE) {
        LOG_INFO("SipServer: 准备发送INVITE请求到 {}:{}, 消息长度={}", peer_ip, peer_port, request.length());
        LOG_DEBUG("SipServer: INVITE请求内容（前500字符）: {}", request.substr(0, 500));
    }
    
    ssize_t sent = sendto(socket_fd_, request.c_str(), request.length(), 0,
                         (struct sockaddr*)&addr, sizeof(addr));
    if (sent < 0) {
        LOG_ERROR("SipServer: 发送请求失败: {}, 目标地址={}:{}", strerror(errno), peer_ip, peer_port);
        total_errors_++;
        return false;
    }
    
    if (method == SipMethod::INVITE) {
        LOG_INFO("SipServer: INVITE请求已发送到 {}:{}, 实际发送字节数={}, 消息长度={}", peer_ip, peer_port, sent, request.length());
    } else {
        LOG_DEBUG("SipServer: 发送请求成功, 目标地址={}:{}, 消息长度={}", peer_ip, peer_port, sent);
    }
    total_messages_sent_++;
    return true;
}

} // namespace gateway

