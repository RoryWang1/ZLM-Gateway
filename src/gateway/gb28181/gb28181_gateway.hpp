#ifndef GATEWAY_GB28181_GB28181_GATEWAY_HPP
#define GATEWAY_GB28181_GB28181_GATEWAY_HPP

#include "gateway/base/gateway_base.hpp"
#include "gateway/gb28181/sip_server.hpp"
#include "gateway/gb28181/gb28181_device.hpp"
#include "streaming/zlmediakit/zlm_client.hpp"
#include "streaming/stream_manager.hpp"
#include "config/config_loader.hpp"
#include <string>
#include <vector>
#include <memory>
#include <map>
#include <mutex>
#include <thread>
#include <atomic>
#include <chrono>

namespace gateway {

/**
 * @brief GB28181 Gateway
 * 
 * 功能：
 * 1. SIP服务器（处理设备注册、心跳、控制）
 * 2. 设备管理（设备列表、状态跟踪）
 * 3. 流管理（启动/停止流，整合SIP和ZLM RTP服务器）
 */
class GB28181Gateway : public GatewayBase {
public:
    /**
     * @brief 构造函数
     * @param config 配置对象
     * @param zlm_client ZLMediaKit 客户端
     * @param stream_manager 流管理器（可选）
     */
    GB28181Gateway(
        std::shared_ptr<config::Config> config,
        std::shared_ptr<streaming::ZLMClient> zlm_client,
        std::shared_ptr<streaming::StreamManager> stream_manager = nullptr
    );
    
    /**
     * @brief 析构函数
     */
    ~GB28181Gateway();
    
    // GatewayBase 接口实现
    Result<void> Start(const std::string& source_url,
                      const std::string& target_app,
                      const std::string& target_stream,
                      const std::string& output_protocol = "") override;
    
    Result<void> Stop(const std::string& target_app,
                     const std::string& target_stream) override;
    
    bool IsRunning(const std::string& target_app,
                  const std::string& target_stream) override;
    
    /**
     * @brief 根据设备ID和通道ID停止流
     * @param device_id 设备ID
     * @param channel_id 通道ID（可选，为空则停止该设备所有流）
     * @return 停止的流数量
     */
    int StopStreamByDevice(const std::string& device_id, const std::string& channel_id = "");
    
    GatewayStatus GetStatus(const std::string& target_app,
                           const std::string& target_stream) override;
    
    std::string GetProtocol() const override { return "gb28181"; }
    
    // 设备发现接口（与其他Gateway保持一致）
    /**
     * @brief 发现GB28181设备
     * 
     * 注意：GB28181设备通过SIP REGISTER主动注册，此方法主要用于：
     * 1. 查询已注册的设备列表
     * 2. 触发设备重新注册（如果支持）
     * 3. 前端展示设备列表
     * 
     * 实际设备发现是通过SIP REGISTER自动完成的
     */
    std::vector<GB28181Device> DiscoverDevices(int timeout_seconds = 5);
    
    /**
     * @brief 获取所有设备列表
     */
    std::vector<GB28181Device> ListDevices() const;
    
    /**
     * @brief 获取设备信息
     */
    GB28181Device GetDevice(const std::string& device_id) const;
    
    /**
     * @brief 手动添加设备（用于测试或已知设备）
     */
    std::string AddDevice(const GB28181Device& device);
    
    /**
     * @brief 删除设备
     */
    bool RemoveDevice(const std::string& device_id);
    
    /**
     * @brief 更新设备认证信息
     */
    bool UpdateDeviceCredentials(const std::string& device_id,
                                const std::string& username,
                                const std::string& password);
    
    /**
     * @brief 启动SIP服务器
     */
    bool StartSipServer();
    
    /**
     * @brief 停止SIP服务器
     */
    void StopSipServer();
    
    /**
     * @brief 获取RTP服务器信息
     */
    streaming::RtpServerInfo GetRtpServerInfo(const std::string& target_app,
                                             const std::string& target_stream) const;
    
    /**
     * @brief 获取RTP流ID（带时间戳的实际流名称）
     * @param target_app 目标应用名
     * @param target_stream 目标流名（不带时间戳）
     * @return RTP流ID（带时间戳），如果不存在返回空字符串
     */
    std::string GetRtpStreamId(const std::string& target_app,
                              const std::string& target_stream) const;

private:
    /**
     * @brief 流信息
     */
    struct StreamInfo {
        std::string device_id;
        std::string channel_id;
        std::string source_url;
        std::string target_app;
        std::string target_stream;
        std::string output_protocol;
        std::string rtp_stream_id;  // RTP服务器stream_id
        int rtp_port = 0;           // RTP端口
        std::string call_id;        // SIP Call-ID
        int device_sip_port = 0;    // 设备SIP端口（从设备注册时保存，用于发送BYE请求）
        GatewayStatus status = GatewayStatus::Stopped;
        std::chrono::system_clock::time_point created_time;  // 流创建时间（用于超时检测）
        bool sip_200_ok_received = false;  // 是否已收到200 OK响应
        std::chrono::system_clock::time_point sip_200_ok_time;  // 收到200 OK的时间（用于RTP流推送超时检测）
    };
    
    // ParseSourceUrl 方法已移除，改用 utils::url_parser::ParseDeviceURL
    
    /**
     * @brief 处理设备注册（SIP服务器回调）
     */
    void OnDeviceRegister(const std::string& device_id,
                         const std::string& ip,
                         int port,
                         int expires);
    
    /**
     * @brief 处理设备心跳（SIP服务器回调）
     */
    void OnDeviceHeartbeat(const std::string& device_id);
    
    /**
     * @brief 发送SIP INVITE请求
     */
    bool SendInvite(const GB28181Device& device,
                   const std::string& channel_id,
                   const streaming::RtpServerInfo& rtp_info,
                   std::string& call_id);
    
    /**
     * @brief 发送SIP BYE请求
     */
    bool SendBye(const std::string& call_id,
                const std::string& device_ip,
                int device_port);
    
    /**
     * @brief 构造SDP响应
     */
    std::string BuildSdpResponse(const streaming::RtpServerInfo& rtp_info,
                                const std::string& local_ip);
    
    /**
     * @brief 解析SDP请求
     */
    bool ParseSdpRequest(const std::string& sdp_body,
                       std::string& device_ip,
                       int& device_port);
    
    /**
     * @brief 定期检查设备心跳超时
     */
    void CheckDeviceHeartbeat();
    
    /**
     * @brief 定期检查流状态验证
     */
    void CheckStreamStatus();
    
    /**
     * @brief 获取本地IP地址（用于SDP）
     */
    std::string GetLocalIP() const;
    
    /**
     * @brief 处理INVITE请求（设备主动推流）
     */
    bool OnInviteRequest(const std::string& device_id,
                        const std::string& channel_id,
                        const std::string& call_id,
                        const std::string& sdp_body,
                        const std::string& peer_ip,
                        int peer_port,
                        std::string& rtp_stream_id,
                        streaming::RtpServerInfo& rtp_info);
    
    /**
     * @brief 处理BYE请求（设备主动停止推流）
     */
    bool OnByeRequest(const std::string& call_id);
    
    /**
     * @brief 处理SIP响应消息（处理设备对INVITE请求的响应）
     */
    void OnSipResponse(const std::string& call_id, int status_code, const std::string& reason_phrase, int peer_port);
    
    /**
     * @brief 启动设备流（内部方法，解耦合）
     * @param device_id 设备ID
     * @param channel_id 通道ID
     * @param target_app 目标应用名
     * @param target_stream 目标流名
     * @param output_protocol 输出协议
     * @param source_url 源URL
     * @return 是否成功
     */
    Result<void> StartDeviceStream(const std::string& device_id,
                          const std::string& channel_id,
                          const std::string& target_app,
                          const std::string& target_stream,
                          const std::string& output_protocol,
                          const std::string& source_url);
    
    /**
     * @brief 创建RTP服务器（带重试机制）
     * @param rtp_stream_id RTP流ID
     * @param rtp_info 输出：RTP服务器信息
     * @return 是否成功
     */
    bool CreateRtpServerWithRetry(const std::string& rtp_stream_id,
                                  streaming::RtpServerInfo& rtp_info,
                                  const std::string& target_app = "");
    
    // 配置
    std::shared_ptr<config::Config> config_;
    std::shared_ptr<streaming::ZLMClient> zlm_client_;
    std::shared_ptr<streaming::StreamManager> stream_manager_;
    
    // SIP服务器
    std::unique_ptr<SipServer> sip_server_;
    bool sip_server_started_ = false;
    
    // 设备管理
    std::map<std::string, GB28181Device> devices_;
    mutable std::mutex devices_mutex_;
    
    // 流管理
    std::map<std::string, StreamInfo> streams_;
    mutable std::mutex streams_mutex_;
    
    // 心跳检查线程
    std::thread heartbeat_check_thread_;
    std::atomic<bool> heartbeat_check_running_;
    
    // 流状态检查线程
    std::thread stream_status_check_thread_;
    std::atomic<bool> stream_status_check_running_;
    
    // 配置参数
    std::string server_id_;
    std::string domain_;
    int heartbeat_timeout_seconds_;
    bool auto_remove_offline_;
    int default_tcp_mode_;
    bool default_enable_rtcp_;
    std::string rtp_port_range_;
    int stream_start_timeout_seconds_;  // 流启动超时时间（秒）
};

} // namespace gateway

#endif // GATEWAY_GB28181_GB28181_GATEWAY_HPP

