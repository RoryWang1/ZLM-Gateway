#ifndef GATEWAY_LOCAL_CAMERA_LOCAL_CAMERA_GATEWAY_HPP
#define GATEWAY_LOCAL_CAMERA_LOCAL_CAMERA_GATEWAY_HPP

#include "gateway/base/gateway_base.hpp"
#include "gateway/local_camera/local_camera_device.hpp"
#include "gateway/utils/smart_stream_processor.hpp"
#include "gateway/utils/ffmpeg_process_helper.hpp"
#include "gateway/utils/bitrate_allocation_helper.hpp"
#include "common/result.hpp"
// ... (rest of includes)
#include <cstring>
#include <memory>
#include <map>
#include <mutex>
#include <atomic>
#include <thread>
#include <vector>
#include <chrono>
#include "utils/camera_detector.hpp"

// 前向声明
namespace gateway {
namespace utils {
class FFmpegCommandBuilder;
}
namespace local_camera {
class DeviceManager;
class DeviceResolver;
class StreamValidator;
class HealthMonitor;
}
}

namespace streaming {
class ZLMClient;
}

namespace config {
struct Config;
}

namespace process {
class ProcessManager;
}

namespace api {
class WebSocketServer;
}

namespace streaming {
class StreamManager;
}

namespace utils {
class BitrateAllocator;
}

namespace gateway {

/**
 * @brief 流信息（用于 LocalCameraGateway）
 */
struct LocalCameraStreamInfo {
    std::string device_id;
    int camera_index;
    std::string camera_name;  // 摄像头名称，用于音频设备匹配
    std::string target_app;
    std::string target_stream;
    std::string output_protocol;
    int pid = 0;
    GatewayStatus status = GatewayStatus::Stopped;
    
    // 统计信息
    std::chrono::system_clock::time_point start_time;  // 流启动时间
    int recover_attempts = 0;  // 恢复尝试次数
    std::chrono::system_clock::time_point last_recover_time;  // 上次恢复时间
};

/**
 * @brief Local Camera Gateway
 * 
 * 管理本地USB摄像头设备，通过FFmpeg推流到ZLMediaKit
 */
class LocalCameraGateway : public GatewayBase {
public:
    /**
     * @brief 构造函数
     * @param config 配置对象
     * @param zlm_client ZLMediaKit 客户端
     * @param process_manager 进程管理器（可选，如果提供则使用统一的进程管理）
     * @param websocket_server WebSocket 服务器（可选，用于推送设备更新）
     */
    LocalCameraGateway(std::shared_ptr<config::Config> config,
                      std::shared_ptr<streaming::ZLMClient> zlm_client,
                      std::shared_ptr<process::ProcessManager> process_manager = nullptr,
                      std::shared_ptr<api::WebSocketServer> websocket_server = nullptr,
                      std::shared_ptr<streaming::StreamManager> stream_manager = nullptr,
                      std::shared_ptr<::utils::BitrateAllocator> bitrate_allocator = nullptr);

    /**
     * @brief 析构函数
     */
    ~LocalCameraGateway();

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

    std::string GetProtocol() const override { return "local-camera"; }
    std::string GetGatewayType() const override { return "local_camera_gateway"; }

    /**
     * @brief 发现本地摄像头设备
     * @return 摄像头设备列表
     */
    Result<std::vector<LocalCameraDevice>> DiscoverCameras();
    
    // ... (rest of methods) ...



    /**
     * @brief 获取所有已发现的摄像头设备（会重新扫描并更新设备列表）
     * @param refresh 是否重新扫描设备（默认true）
     * @return 摄像头设备列表
     */
    Result<std::vector<LocalCameraDevice>> ListCameras(bool refresh = true);

    /**
     * @brief 获取指定设备ID的摄像头信息
     * @param device_id 设备ID
     * @return 摄像头设备信息
     */
    Result<LocalCameraDevice> GetCamera(const std::string& device_id) const;

    /**
     * @brief 从设备ID获取摄像头索引
     * @param device_id 设备ID
     * @return 摄像头索引
     */
    Result<int> GetCameraIndexFromDeviceID(const std::string& device_id) const;

    /**
     * @brief 启动摄像头流
     * @param device_id 设备ID
     * @param target_app 目标应用名
     * @param target_stream 目标流名
     * @param output_protocol 输出协议
     * @return 是否成功 (Result<void>)
     */
    Result<void> StartCameraStream(const std::string& device_id,
                          const std::string& target_app,
                          const std::string& target_stream,
                          const std::string& output_protocol = "webrtc");

    /**
     * @brief 停止摄像头流
     * @param target_app 目标应用名
     * @param target_stream 目标流名
     * @return 是否成功 (Result<void>)
     */
    Result<void> StopCameraStream(const std::string& target_app,
                         const std::string& target_stream);
    
    /**
     * @brief 查询设备能力（支持的分辨率和帧率）
     * @param device_id 设备ID
     * @return 设备能力信息（分辨率列表和帧率列表）
     */
    Result<std::pair<std::vector<std::string>, std::vector<int>>> QueryDeviceCapabilities(const std::string& device_id) const;

    /**
     * @brief 检查设备是否被占用
     * @param device_id 设备ID
     * @return 如果设备被占用，返回占用该设备的流信息（app/stream），否则返回空字符串
     */
    std::string CheckDeviceOccupied(const std::string& device_id) const;

private:
    // 使用命名空间级别的 LocalCameraStreamInfo
    using StreamInfo = LocalCameraStreamInfo;


    /**
     * @brief 构建 FFmpeg 命令
     * @param info 流信息
     * @return FFmpeg 命令字符串
     */
    std::string BuildFFmpegCommand(const StreamInfo& info);

    /**
     * @brief 构建 FFmpeg 命令（使用智能分配的码率）
     * @param info 流信息
     * @param bitrate_kbps 智能分配的码率（kbps）
     * @return FFmpeg 命令字符串
     */
    std::string BuildFFmpegCommandWithBitrate(const StreamInfo& info, int bitrate_kbps);


    
    std::shared_ptr<config::Config> config_;
    std::shared_ptr<streaming::ZLMClient> zlm_client_;
    std::shared_ptr<process::ProcessManager> process_manager_;
    std::shared_ptr<api::WebSocketServer> websocket_server_;
    std::shared_ptr<streaming::StreamManager> stream_manager_;  // 流管理器
    std::unique_ptr<::utils::CameraDetector> camera_detector_;
    
    // 设备管理器（前向声明，在 cpp 文件中包含完整定义）
    std::unique_ptr<local_camera::DeviceManager> device_manager_;
    
    // 设备解析器（前向声明，在 cpp 文件中包含完整定义）
    std::unique_ptr<local_camera::DeviceResolver> device_resolver_;
    
    // 流启动验证器（前向声明，在 cpp 文件中包含完整定义）
    std::unique_ptr<local_camera::StreamValidator> stream_validator_;
    
    // 健康监控器（前向声明，在 cpp 文件中包含完整定义）
    std::unique_ptr<local_camera::HealthMonitor> health_monitor_;
    
    
    // 流管理
    std::map<std::string, StreamInfo> streams_;  // stream_id -> StreamInfo
    mutable std::mutex streams_mutex_;
    
    std::string ffmpeg_path_;
    std::string zlm_rtmp_url_;  // ZLMediaKit RTMP 推流地址
    
    // FFmpeg 命令构建器
    std::unique_ptr<gateway::utils::FFmpegCommandBuilder> ffmpeg_builder_;
    
    // 智能码率分配器
    std::shared_ptr<::utils::BitrateAllocator> bitrate_allocator_;
    
    // 通用辅助类
    std::unique_ptr<gateway::utils::SmartStreamProcessor> smart_processor_;
    std::unique_ptr<gateway::utils::FFmpegProcessHelper> ffmpeg_helper_;
    std::unique_ptr<gateway::utils::BitrateAllocationHelper> bitrate_helper_;
    
    // 码率分配更新线程
    std::thread bitrate_update_thread_;
    std::atomic<bool> bitrate_update_running_{false};
    
    /**
     * @brief 码率分配更新循环线程
     */
    void BitrateUpdateLoop();
};

} // namespace gateway

#endif // GATEWAY_LOCAL_CAMERA_LOCAL_CAMERA_GATEWAY_HPP

