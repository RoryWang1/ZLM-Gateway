#ifndef GATEWAY_ONVIF_ONVIF_GATEWAY_HPP
#define GATEWAY_ONVIF_ONVIF_GATEWAY_HPP

#include "gateway/base/gateway_base.hpp"
#include "gateway/utils/smart_stream_processor.hpp"
#include "gateway/utils/ffmpeg_process_helper.hpp"
#include "gateway/utils/bitrate_allocation_helper.hpp"
#include <string>
#include <vector>
#include <memory>
#include <map>
#include <mutex>
#include <atomic>
#include <chrono>

namespace streaming {
class ZLMClient;
}

namespace config {
struct Config;
}

namespace gateway {

/**
 * @brief ONVIF 设备信息
 */
struct ONVIFDevice {
    std::string id;                      // 设备 ID（唯一标识）
    std::string xaddr;                   // 设备服务地址（XAddr）
    std::vector<std::string> types;      // 设备类型列表
    std::vector<std::string> scopes;     // 设备范围列表
    std::string manufacturer;            // 制造商
    std::string model;                   // 型号
    std::string serial_number;           // 序列号
    std::string hardware_id;             // 硬件 ID
    std::string username;                // 用户名（用于认证）
    std::string password;                // 密码（用于认证）
    std::vector<std::string> rtsp_urls;  // RTSP 流地址列表（缓存）
    std::chrono::system_clock::time_point last_seen;  // 最后发现时间
    std::chrono::system_clock::time_point rtsp_urls_expire_time;  // RTSP 地址过期时间
};

/**
 * @brief ONVIF Gateway
 * 
 * 功能：
 * 1. 使用 WS-Discovery 协议发现 ONVIF 设备
 * 2. 调用 ONVIF Media API 获取设备的 RTSP 流地址
 * 3. 管理设备的流（直接推流到 ZLMediaKit，因为 RTSP 是原生协议）
 */
class ONVIFGateway : public GatewayBase {
public:
    /**
     * @brief 构造函数
     * @param config 配置对象
     * @param zlm_client ZLMediaKit 客户端
     * @param process_manager 进程管理器（可选，用于转码）
     * @param stream_manager 流管理器（可选）
     */
    ONVIFGateway(std::shared_ptr<config::Config> config,
                 std::shared_ptr<streaming::ZLMClient> zlm_client,
                 std::shared_ptr<process::ProcessManager> process_manager = nullptr,
                 std::shared_ptr<streaming::StreamManager> stream_manager = nullptr);

    /**
     * @brief 析构函数
     */
    ~ONVIFGateway();

    // GatewayBase 接口实现
    Result<void> Start(const std::string& source_url,
                      const std::string& target_app,
                      const std::string& target_stream,
                      const std::string& output_protocol = "") override;

    Result<void> Stop(const std::string& target_app,
                     const std::string& target_stream) override;

    bool IsRunning(const std::string& target_app,
                  const std::string& target_stream) override;

    GatewayStatus GetStatus(const std::string& target_app,
                           const std::string& target_stream) override;

    std::string GetProtocol() const override { return "onvif"; }

    /**
     * @brief 发现 ONVIF 设备
     * @param timeout_seconds 超时时间（秒），默认 5 秒
     * @return 发现的设备列表
     */
    std::vector<ONVIFDevice> DiscoverDevices(int timeout_seconds = 5);

    /**
     * @brief 获取所有设备列表
     * @return 设备列表
     */
    std::vector<ONVIFDevice> ListDevices() const;

    /**
     * @brief 获取设备信息
     * @param device_id 设备 ID
     * @return 设备信息，不存在返回空对象（id 为空）
     */
    ONVIFDevice GetDevice(const std::string& device_id) const;

    /**
     * @brief 手动添加设备（用于测试，绕过 WS-Discovery）
     * @param device 设备信息
     * @return 设备 ID
     */
    std::string AddDevice(const ONVIFDevice& device);

    /**
     * @brief 删除设备
     * @param device_id 设备 ID
     * @return 是否成功
     */
    bool RemoveDevice(const std::string& device_id);

    /**
     * @brief 更新设备认证信息
     * @param device_id 设备 ID
     * @param username 用户名
     * @param password 密码
     * @return 是否成功
     */
    bool UpdateDeviceCredentials(const std::string& device_id,
                                const std::string& username,
                                const std::string& password);

    /**
     * @brief 获取设备的 RTSP 流地址
     * @param device_id 设备 ID
     * @param profile_token 配置令牌（可选，为空时使用第一个配置）
     * @return RTSP 流地址列表
     */
    std::vector<std::string> GetDeviceRTSPURLs(const std::string& device_id,
                                               const std::string& profile_token = "");

    /**
     * @brief 启动设备流
     * @param device_id 设备 ID
     * @param profile_token 配置令牌（可选）
     * @param target_app 目标应用名
     * @param target_stream 目标流名
     * @return 是否成功
     */
    Result<void> StartDeviceStream(const std::string& device_id,
                          const std::string& profile_token,
                          const std::string& target_app,
                          const std::string& target_stream);

    /**
     * @brief 停止设备流
     * @param target_app 目标应用名
     * @param target_stream 目标流名
     * @return 是否成功
     */
    Result<void> StopDeviceStream(const std::string& target_app,
                         const std::string& target_stream);

private:
    /**
     * @brief 流信息
     */
    struct StreamInfo {
        std::string device_id;
        std::string profile_token;
        std::string rtsp_url;
        std::string target_app;
        std::string target_stream;
        std::string output_protocol; // 输出协议
        std::string source_audio_codec; // 源音频编码
        std::string source_video_codec; // 源视频编码
        int source_width = 0;  // 源流宽度（0表示未检测到）
        int source_height = 0;  // 源流高度（0表示未检测到）
        bool video_only_transcode = false;  // 是否只转码音频（视频兼容，音频不兼容）
        int pid = 0;  // FFmpeg 进程 PID（如果使用了转码）
        GatewayStatus status = GatewayStatus::Stopped;
    };

    /**
     * @brief 构建 FFmpeg 命令（如果需要转码）
     * @param info 流信息
     * @param stream_info_result 流信息检测结果
     * @param bitrate_kbps 码率（kbps，0表示使用默认值）
     */
    std::string BuildFFmpegCommand(const StreamInfo& info, 
                                   const gateway::utils::StreamInfoResult& stream_info_result,
                                   int bitrate_kbps = 0);

    /**
     * @brief 使用 WS-Discovery 协议发现设备
     * @param timeout_seconds 超时时间（秒）
     * @return 发现的设备列表
     */
    std::vector<ONVIFDevice> DiscoverDevicesWSDiscovery(int timeout_seconds);

    /**
     * @brief 发送 WS-Discovery Probe 消息
     * @return 设备响应列表
     */
    std::vector<ONVIFDevice> SendWSDiscoveryProbe(int timeout_seconds);

    /**
     * @brief 解析 WS-Discovery ProbeMatch 响应
     * @param response_xml 响应 XML
     * @return 设备信息
     */
    ONVIFDevice ParseProbeMatchResponse(const std::string& response_xml);

    /**
     * @brief 调用 ONVIF Media API GetProfiles
     * @param device 设备信息
     * @return 配置令牌列表
     */
    std::vector<std::string> GetProfiles(const ONVIFDevice& device);

    /**
     * @brief 调用 ONVIF Media API GetStreamUri
     * @param device 设备信息
     * @param profile_token 配置令牌
     * @return RTSP 流地址
     */
    std::string GetStreamUri(const ONVIFDevice& device, const std::string& profile_token);

    /**
     * @brief 构建 SOAP 请求
     * @param action SOAP Action
     * @param body SOAP Body
     * @return SOAP 请求 XML
     */
    std::string BuildSOAPRequest(const std::string& action, const std::string& body);

    /**
     * @brief 发送 SOAP 请求
     * @param url 请求 URL
     * @param soap_action SOAP Action
     * @param soap_body SOAP Body
     * @param username 用户名（可选）
     * @param password 密码（可选）
     * @return 响应 XML
     */
    std::string SendSOAPRequest(const std::string& url,
                               const std::string& soap_action,
                               const std::string& soap_body,
                               const std::string& username = "",
                               const std::string& password = "");

    /**
     * @brief 生成设备 ID（基于 XAddr）
     */
    std::string GenerateDeviceID(const std::string& xaddr) const;

    /**
     * @brief 生成 UUID
     */
    std::string GenerateUUID() const;

    std::shared_ptr<config::Config> config_;
    std::shared_ptr<streaming::ZLMClient> zlm_client_;
    std::shared_ptr<process::ProcessManager> process_manager_;  // 进程管理器（用于转码）
    std::shared_ptr<streaming::StreamManager> stream_manager_;
    
    // 通用辅助类
    std::unique_ptr<gateway::utils::SmartStreamProcessor> smart_processor_;
    std::unique_ptr<gateway::utils::FFmpegProcessHelper> ffmpeg_helper_;
    std::unique_ptr<gateway::utils::BitrateAllocationHelper> bitrate_helper_;
    
    // 设备管理
    std::map<std::string, ONVIFDevice> devices_;  // device_id -> ONVIFDevice
    mutable std::mutex devices_mutex_;
    
    // 流管理
    std::map<std::string, StreamInfo> streams_;  // stream_id -> StreamInfo
    mutable std::mutex streams_mutex_;
    
    // FFmpeg 路径
    std::string ffmpeg_path_;
    std::string ffprobe_path_;
    std::string zlm_rtsp_url_; // 用于推流到 ZLM
    
    // RTSP 地址缓存时间（秒），默认 5 分钟
    static constexpr int RTSP_URL_CACHE_SECONDS = 300;
};

} // namespace gateway

#endif // GATEWAY_ONVIF_ONVIF_GATEWAY_HPP

