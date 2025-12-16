#ifndef GATEWAY_RTSP_RTSP_GATEWAY_HPP
#define GATEWAY_RTSP_RTSP_GATEWAY_HPP

#include "gateway/base/gateway_base.hpp"
#include "gateway/utils/smart_stream_processor.hpp"
#include "gateway/utils/ffmpeg_process_helper.hpp"
#include "gateway/utils/bitrate_allocation_helper.hpp"
#include "gateway/utils/ffmpeg_command_builder.hpp"
#include <cstring>
#include <memory>
#include <map>
#include <mutex>
#include <atomic>

namespace streaming {
class ZLMClient;
}

namespace config {
struct Config;
}

namespace process {
class ProcessManager;
class FFmpegExecutor;
}


namespace streaming {
class StreamManager;
}

namespace utils {
class BitrateAllocator;
}

namespace gateway {

/**
 * @brief RTSP Gateway
 * 
 * 使用 FFmpeg 从 RTSP 源拉流并推送到 ZLMediaKit
 */
class RTSPGateway : public GatewayBase {
public:
    /**
     * @brief 构造函数
     * @param config 配置对象
     * @param zlm_client ZLMediaKit 客户端
     * @param process_manager 进程管理器（可选，如果提供则使用统一的进程管理）
     * @param stream_manager 流管理器（可选，用于码率分配）
     * @param bitrate_allocator 码率分配器（可选，用于智能码率分配）
     */
    RTSPGateway(std::shared_ptr<config::Config> config,
                std::shared_ptr<streaming::ZLMClient> zlm_client,
                std::shared_ptr<process::ProcessManager> process_manager = nullptr,
                std::shared_ptr<streaming::StreamManager> stream_manager = nullptr,
                std::shared_ptr<::utils::BitrateAllocator> bitrate_allocator = nullptr);

    /**
     * @brief 析构函数
     */
    ~RTSPGateway();

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

    std::string GetProtocol() const override { return "rtsp"; }

private:
    /**
     * @brief 流信息
     */
    struct StreamInfo {
        std::string source_url;
        std::string target_app;
        std::string target_stream;
        std::string output_protocol; // 输出协议
        std::string source_audio_codec; // 探测到的音频 codec 名称
        std::string source_video_codec; // 探测到的视频 codec 名称
        int source_width = 0;  // 源流宽度（0表示未检测到）
        int source_height = 0;  // 源流高度（0表示未检测到）
        bool video_only_transcode = false;  // 是否只转码音频（视频兼容，音频不兼容）
        int pid = 0;  // FFmpeg 进程 PID
        GatewayStatus status = GatewayStatus::Stopped;
    };



    std::shared_ptr<config::Config> config_;
    std::shared_ptr<streaming::ZLMClient> zlm_client_;
    std::shared_ptr<process::ProcessManager> process_manager_;  // 进程管理器（可选）
    std::shared_ptr<streaming::StreamManager> stream_manager_;  // 流管理器（可选）
    
    // 通用辅助类
    std::unique_ptr<gateway::utils::SmartStreamProcessor> smart_processor_;  // 智能流处理器
    std::unique_ptr<gateway::utils::FFmpegProcessHelper> ffmpeg_helper_;  // FFmpeg 进程管理辅助
    std::unique_ptr<gateway::utils::BitrateAllocationHelper> bitrate_helper_;  // 码率分配辅助
    // 统一 FFmpeg 命令构建器
    std::unique_ptr<gateway::utils::FFmpegCommandBuilder> ffmpeg_builder_;
    
    std::map<std::string, StreamInfo> streams_;  // stream_id -> StreamInfo
    std::mutex streams_mutex_;
    std::string ffmpeg_path_; // path to ffmpeg
    std::string ffprobe_path_; // path to ffprobe
    std::string zlm_rtsp_url_;  // ZLMediaKit RTSP 推流地址

};

} // namespace gateway

#endif // GATEWAY_RTSP_RTSP_GATEWAY_HPP
