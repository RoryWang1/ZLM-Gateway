#ifndef GATEWAY_ISAPI_ISAPI_GATEWAY_HPP
#define GATEWAY_ISAPI_ISAPI_GATEWAY_HPP

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
class StreamManager;
}

namespace config {
struct Config;
}

namespace gateway {

/**
 * @brief ISAPI 设备信息
 */
struct ISAPIDevice {
    std::string id;                      // 设备 ID（唯一标识）
    std::string base_url;                // 设备基础 URL（如 http://192.168.1.100:80）
    std::string manufacturer;            // 制造商（通常为 "Hikvision"）
    std::string model;                   // 型号
    std::string serial_number;           // 序列号
    std::string firmware_version;        // 固件版本
    std::string username;                // 用户名（用于认证）
    std::string password;                // 密码（用于认证）
    std::vector<std::string> rtsp_urls;  // RTSP 流地址列表（缓存）
    std::chrono::system_clock::time_point last_seen;  // 最后发现时间
    std::chrono::system_clock::time_point rtsp_urls_expire_time;  // RTSP 地址过期时间
};

/**
 * @brief ISAPI Gateway
 * 
 * 功能：
 * 1. 发现海康 ISAPI 设备（通过 HTTP API 扫描或手动添加）
 * 2. 调用 ISAPI API 获取设备的 RTSP 流地址
 * 3. 管理设备的流（直接推流到 ZLMediaKit，因为 RTSP 是原生协议）
 */
class ISAPIGateway : public GatewayBase {
public:
    /**
     * @brief 构造函数
     * @param config 配置对象
     * @param zlm_client ZLMediaKit 客户端
     * @param process_manager 进程管理器（可选，用于转码）
     * @param stream_manager 流管理器（可选）
     */
    ISAPIGateway(std::shared_ptr<config::Config> config,
                 std::shared_ptr<streaming::ZLMClient> zlm_client,
                 std::shared_ptr<process::ProcessManager> process_manager = nullptr,
                 std::shared_ptr<streaming::StreamManager> stream_manager = nullptr);

    /**
     * @brief 析构函数
     */
    ~ISAPIGateway();

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

    std::string GetProtocol() const override { return "isapi"; }

    // ISAPI 特定接口
    /**
     * @brief 发现 ISAPI 设备（通过 HTTP API 扫描网络）
     * @param ip_range IP 地址范围（如 "192.168.1.0/24"），为空则扫描常见网段
     * @param timeout_seconds 超时时间（秒）
     * @return 发现的设备列表
     */
    std::vector<ISAPIDevice> DiscoverDevices(const std::string& ip_range = "",
                                             int port = 80, int timeout_seconds = 5);

    /**
     * @brief 获取所有设备列表
     * @return 设备列表
     */
    std::vector<ISAPIDevice> ListDevices() const;

    /**
     * @brief 获取设备信息
     * @param device_id 设备 ID
     * @return 设备信息，不存在返回空对象（id 为空）
     */
    ISAPIDevice GetDevice(const std::string& device_id) const;

    /**
     * @brief 手动添加设备
     * @param device 设备信息
     * @return 设备 ID
     */
    std::string AddDevice(const ISAPIDevice& device);

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
     * @param channel_id 通道 ID（可选，为空则获取所有通道）
     * @return RTSP 流地址列表
     */
    std::vector<std::string> GetDeviceRTSPURLs(const std::string& device_id,
                                               const std::string& channel_id = "");

private:
    /**
     * @brief 流信息
     */
    struct StreamInfo {
        std::string device_id;
        std::string channel_id;
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
     * @brief 启动设备流
     * @param device_id 设备 ID
     * @param channel_id 通道 ID
     * @param target_app 目标应用名
     * @param target_stream 目标流名
     * @return 是否成功
     */
    Result<void> StartDeviceStream(const std::string& device_id,
                          const std::string& channel_id,
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

    /**
     * @brief 通过 HTTP API 扫描网络发现设备
     * @param ip_range IP 地址范围
     * @param timeout_seconds 超时时间（秒）
     * @return 发现的设备列表
     */
    std::vector<ISAPIDevice> DiscoverDevicesHTTP(const std::string& ip_range,
                                                 int port, int timeout_seconds);

    /**
     * @brief 检查指定 IP 是否为 ISAPI 设备
     * @param ip IP 地址
     * @param port 端口（默认 80）
     * @param timeout_seconds 超时时间（秒）
     * @return 设备信息，如果不是设备则返回空对象（id 为空）
     */
    ISAPIDevice ProbeDevice(const std::string& ip, int port = 80, int timeout_seconds = 2);

    /**
     * @brief 调用 ISAPI API 获取设备信息
     * @param base_url 设备基础 URL
     * @param username 用户名
     * @param password 密码
     * @return 设备信息
     */
    ISAPIDevice GetDeviceInfo(const std::string& base_url,
                             const std::string& username = "",
                             const std::string& password = "");

    /**
     * @brief 调用 ISAPI API 获取通道列表
     * @param device 设备信息
     * @return 通道 ID 列表
     */
    std::vector<std::string> GetChannels(const ISAPIDevice& device);

    /**
     * @brief 调用 ISAPI API 获取指定通道的 RTSP 流地址
     * @param device 设备信息
     * @param channel_id 通道 ID
     * @return RTSP 流地址
     */
    std::string GetStreamURI(const ISAPIDevice& device, const std::string& channel_id);

    /**
     * @brief 发送 ISAPI HTTP 请求
     * @param url 请求 URL
     * @param method HTTP 方法（GET/POST/PUT/DELETE）
     * @param body 请求体（可选）
     * @param username 用户名（用于认证）
     * @param password 密码（用于认证）
     * @return 响应内容
     */
    std::string SendISAPIRequest(const std::string& url,
                                const std::string& method = "GET",
                                const std::string& body = "",
                                const std::string& username = "",
                                const std::string& password = "");

    /**
     * @brief 生成设备 ID
     * @param base_url 设备基础 URL
     * @return 设备 ID
     */
    std::string GenerateDeviceID(const std::string& base_url) const;

    /**
     * @brief 解析 IP 地址范围
     * @param ip_range IP 地址范围（如 "192.168.1.0/24"）
     * @return IP 地址列表
     */
    std::vector<std::string> ParseIPRange(const std::string& ip_range) const;

    std::shared_ptr<config::Config> config_;
    std::shared_ptr<streaming::ZLMClient> zlm_client_;
    std::shared_ptr<process::ProcessManager> process_manager_;  // 进程管理器（用于转码）
    std::shared_ptr<streaming::StreamManager> stream_manager_;
    
    // 通用辅助类
    std::unique_ptr<gateway::utils::SmartStreamProcessor> smart_processor_;
    std::unique_ptr<gateway::utils::FFmpegProcessHelper> ffmpeg_helper_;
    std::unique_ptr<gateway::utils::BitrateAllocationHelper> bitrate_helper_;

    mutable std::mutex devices_mutex_;
    std::map<std::string, ISAPIDevice> devices_;

    mutable std::mutex streams_mutex_;
    std::map<std::string, StreamInfo> streams_;
    
    // FFmpeg 路径
    std::string ffmpeg_path_;
    std::string ffprobe_path_;
    std::string zlm_rtsp_url_; // 用于推流到 ZLM
};

} // namespace gateway

#endif // GATEWAY_ISAPI_ISAPI_GATEWAY_HPP

