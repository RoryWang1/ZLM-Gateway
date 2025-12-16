#ifndef GATEWAY_GB28181_SIP_SERVER_HPP
#define GATEWAY_GB28181_SIP_SERVER_HPP

#include <string>
#include <memory>
#include <functional>
#include <thread>
#include <atomic>
#include <mutex>
#include <map>
#include <queue>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include "sip_message.hpp"
#include "gb28181_device.hpp"
#include "utils/logger.hpp"
#include "streaming/zlmediakit/zlm_client.hpp"

namespace gateway {

/**
 * @brief SIP 事务信息
 */
struct SipTransaction {
    std::string call_id;
    int cseq = 0;
    std::string method;
    std::string response;  // 缓存的响应
    std::chrono::system_clock::time_point created_time;
    std::string peer_ip;
    int peer_port = 0;
    struct sockaddr_in peer_addr;  // 对端地址（用于重发响应）
};

/**
 * @brief SIP 服务器
 * 
 * 功能：
 * 1. UDP Socket 监听（端口 5060）
 * 2. SIP 消息接收和解析
 * 3. REGISTER/INVITE/BYE/MESSAGE 处理
 * 4. 事务管理和重传处理
 */
class SipServer {
public:
    /**
     * @brief 设备注册回调函数类型
     * @param device_id 设备ID
     * @param ip 设备IP
     * @param port 设备端口
     * @param expires 注册过期时间（秒）
     */
    using DeviceRegisterCallback = std::function<void(
        const std::string& device_id,
        const std::string& ip,
        int port,
        int expires
    )>;
    
    /**
     * @brief 设备心跳回调函数类型
     * @param device_id 设备ID
     */
    using DeviceHeartbeatCallback = std::function<void(const std::string& device_id)>;
    
    /**
     * @brief 设备认证回调函数类型
     * @param device_id 设备ID
     * @param username 输出：用户名（如果设备需要认证）
     * @param password 输出：密码（如果设备需要认证）
     * @return 是否需要认证（true=需要认证，false=不需要认证）
     */
    using DeviceAuthCallback = std::function<bool(
        const std::string& device_id,
        std::string& username,
        std::string& password
    )>;
    
    /**
     * @brief INVITE请求回调函数类型（设备主动推流）
     * @param device_id 设备ID（从From头部提取）
     * @param channel_id 通道ID（从Request-URI提取）
     * @param call_id Call-ID
     * @param sdp_body SDP消息体
     * @param peer_ip 对端IP
     * @param peer_port 对端端口
     * @param rtp_stream_id 输出：RTP服务器stream_id（如果成功创建）
     * @param rtp_info 输出：RTP服务器信息（如果成功创建）
     * @return 是否成功处理（true=成功，false=失败）
     */
    using InviteRequestCallback = std::function<bool(
        const std::string& device_id,
        const std::string& channel_id,
        const std::string& call_id,
        const std::string& sdp_body,
        const std::string& peer_ip,
        int peer_port,
        std::string& rtp_stream_id,
        streaming::RtpServerInfo& rtp_info
    )>;
    
    /**
     * @brief BYE请求回调函数类型（设备主动停止推流）
     * @param call_id Call-ID
     * @param rtp_stream_id RTP服务器stream_id
     * @return 是否成功处理（true=成功，false=失败）
     */
    using ByeRequestCallback = std::function<bool(
        const std::string& call_id,
        const std::string& rtp_stream_id
    )>;
    
    /**
     * @brief 响应消息回调函数类型（处理设备响应）
     * @param call_id Call-ID
     * @param status_code 状态码
     * @param reason_phrase 原因短语
     * @param peer_port 对端端口（设备实际监听的端口，用于发送BYE请求）
     */
    using ResponseCallback = std::function<void(
        const std::string& call_id,
        int status_code,
        const std::string& reason_phrase,
        int peer_port
    )>;
    
    /**
     * @brief 构造函数
     * @param local_ip 本地IP地址
     * @param local_port 本地端口（默认5060）
     * @param server_id 服务器ID（20位国标ID）
     * @param domain 域（10位数字）
     */
    SipServer(
        const std::string& local_ip = "0.0.0.0",
        int local_port = 5060,
        const std::string& server_id = "34020000002000000001",
        const std::string& domain = "3402000000"
    );
    
    /**
     * @brief 析构函数
     */
    ~SipServer();
    
    /**
     * @brief 启动SIP服务器
     * @return 是否成功
     */
    bool Start();
    
    /**
     * @brief 停止SIP服务器
     */
    void Stop();
    
    /**
     * @brief 检查服务器是否运行
     */
    bool IsRunning() const { return is_running_; }
    
    /**
     * @brief 设置设备注册回调
     */
    void SetDeviceRegisterCallback(DeviceRegisterCallback callback) {
        register_callback_ = callback;
    }
    
    /**
     * @brief 设置设备心跳回调
     */
    void SetDeviceHeartbeatCallback(DeviceHeartbeatCallback callback) {
        heartbeat_callback_ = callback;
    }
    
    /**
     * @brief 设置设备认证回调
     */
    void SetDeviceAuthCallback(DeviceAuthCallback callback) {
        auth_callback_ = callback;
    }
    
    /**
     * @brief 设置INVITE请求回调
     */
    void SetInviteRequestCallback(InviteRequestCallback callback) {
        invite_callback_ = callback;
    }
    
    /**
     * @brief 设置BYE请求回调
     */
    void SetByeRequestCallback(ByeRequestCallback callback) {
        bye_callback_ = callback;
    }
    
    /**
     * @brief 设置响应消息回调
     */
    void SetResponseCallback(ResponseCallback callback) {
        response_callback_ = callback;
    }
    
    /**
     * @brief 发送SIP请求
     * @param method 请求方法
     * @param uri 请求URI
     * @param to 目标地址
     * @param headers 额外的头部字段（如果包含Call-ID，将使用该Call-ID）
     * @param body 消息体
     * @param peer_ip 对端IP
     * @param peer_port 对端端口
     * @return 是否成功
     */
    bool SendRequest(
        SipMethod method,
        const std::string& uri,
        const std::string& to,
        const std::map<std::string, std::string>& headers = {},
        const std::string& body = "",
        const std::string& peer_ip = "",
        int peer_port = 0
    );
    
    /**
     * @brief 发送SIP响应
     * @param request 原始请求
     * @param status_code 状态码
     * @param reason_phrase 原因短语
     * @param headers 额外的头部字段
     * @param body 消息体
     * @param peer_ip 对端IP
     * @param peer_port 对端端口
     * @return 是否成功
     */
    bool SendResponse(
        const SipMessage& request,
        int status_code,
        const std::string& reason_phrase,
        const std::map<std::string, std::string>& headers = {},
        const std::string& body = "",
        const std::string& peer_ip = "",
        int peer_port = 0
    );
    
    /**
     * @brief 获取本地IP地址（用于SDP中的c=行）
     * @return 本地IP地址
     */
    std::string GetLocalIP() const;

private:
    /**
     * @brief 服务器主循环（接收UDP消息）
     */
    void ServerLoop();
    
    /**
     * @brief 消息处理循环（处理接收到的消息）
     */
    void MessageLoop();
    
    /**
     * @brief 处理接收到的SIP消息
     */
    void HandleMessage(const SipMessage& msg, const std::string& peer_ip, int peer_port);
    
    /**
     * @brief 处理REGISTER请求
     */
    void HandleRegister(const SipMessage& request, const std::string& peer_ip, int peer_port);
    
    /**
     * @brief 处理MESSAGE请求（心跳）
     */
    void HandleMessageRequest(const SipMessage& request, const std::string& peer_ip, int peer_port);
    
    /**
     * @brief 处理INVITE请求
     */
    void HandleInvite(const SipMessage& request, const std::string& peer_ip, int peer_port);
    
    /**
     * @brief 处理BYE请求
     */
    void HandleBye(const SipMessage& request, const std::string& peer_ip, int peer_port);
    
    /**
     * @brief 处理重传（相同Call-ID和CSeq）
     */
    bool HandleRetransmission(const SipMessage& msg, const std::string& peer_ip, int peer_port);
    
    /**
     * @brief 清理过期事务
     */
    void CleanupExpiredTransactions();
    
    // 配置
    std::string local_ip_;
    int local_port_;
    std::string server_id_;
    std::string domain_;
    
    // Socket
    int socket_fd_ = -1;
    
    // 运行状态
    std::atomic<bool> is_running_;
    std::thread server_thread_;
    std::thread message_thread_;
    
    // 消息队列
    struct MessageItem {
        SipMessage msg;
        std::string peer_ip;
        int peer_port;
    };
    std::queue<MessageItem> message_queue_;
    std::mutex queue_mutex_;
    std::condition_variable queue_cv_;
    
    // 事务管理
    std::map<std::string, SipTransaction> transactions_;  // key: call_id + cseq
    std::mutex transactions_mutex_;
    
    // 回调函数
    DeviceRegisterCallback register_callback_;
    DeviceHeartbeatCallback heartbeat_callback_;
    DeviceAuthCallback auth_callback_;
    InviteRequestCallback invite_callback_;
    ByeRequestCallback bye_callback_;
    ResponseCallback response_callback_;
    
    // 统计信息
    std::atomic<uint64_t> total_messages_received_;
    std::atomic<uint64_t> total_messages_sent_;
    std::atomic<uint64_t> total_errors_;
};

} // namespace gateway

#endif // GATEWAY_GB28181_SIP_SERVER_HPP

