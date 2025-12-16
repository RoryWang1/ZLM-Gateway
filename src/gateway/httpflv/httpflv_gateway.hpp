#ifndef GATEWAY_HTTPFLV_HTTPFLV_GATEWAY_HPP
#define GATEWAY_HTTPFLV_HTTPFLV_GATEWAY_HPP

#include "gateway/base/gateway_base.hpp"
#include "gateway/utils/smart_stream_processor.hpp"
#include "gateway/utils/ffmpeg_process_helper.hpp"
#include "gateway/utils/bitrate_allocation_helper.hpp"
#include "gateway/utils/ffmpeg_command_builder.hpp"
#include <string>
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
class FFprobeDetector;
}


namespace streaming {
class StreamManager;
}

namespace utils {
class BitrateAllocator;
}

namespace gateway {

/**
 * @brief HTTP-FLV Gateway
 * 
 * HTTP-FLV 是 ZLMediaKit 原生支持的协议，直接调用 ZLMediaKit API
 * 无需 FFmpeg 转换，性能最优
 */
class HTTPFLVGateway : public GatewayBase {
public:
    /**
     * @brief 构造函数
     * @param config 配置对象
     * @param zlm_client ZLMediaKit 客户端
     * @param process_manager 进程管理器
     * @param stream_manager 流管理器（可选）
     * @param bitrate_allocator 码率分配器（可选）
     */
    HTTPFLVGateway(std::shared_ptr<config::Config> config,
                   std::shared_ptr<streaming::ZLMClient> zlm_client,
                   std::shared_ptr<process::ProcessManager> process_manager,
                   std::shared_ptr<streaming::StreamManager> stream_manager = nullptr,
                               std::shared_ptr<::utils::BitrateAllocator> bitrate_allocator = nullptr);

    /**
     * @brief 析构函数
     */
    ~HTTPFLVGateway();

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

    std::string GetProtocol() const override { return "http-flv"; }
    std::string GetGatewayType() const override { return "httpflv_gateway"; }

private:
    /**
     * @brief 流信息
     */
    struct StreamInfo {
        std::string source_url;
        std::string target_app;
        std::string target_stream;
        std::string output_protocol; // 输出协议
        GatewayStatus status = GatewayStatus::Stopped;
        int pid = 0; // FFmpeg 进程 ID (如果使用了转码)
        std::string source_audio_codec; // 源音频编码
        std::string source_video_codec; // 源视频编码
        int source_width = 0;  // 源流宽度（0表示未检测到）
        int source_height = 0;  // 源流高度（0表示未检测到）
        bool video_only_transcode = false;  // 是否只转码音频（视频兼容，音频不兼容）
    };

    /**
     * @brief 探测音频编码（已废弃，使用 StreamInfoDetector）
     * @deprecated 使用 stream_info_detector_ 替代
     */
    std::string DetectAudioCodec(const std::string& url) const;




    std::shared_ptr<config::Config> config_;
    std::shared_ptr<streaming::ZLMClient> zlm_client_;
    std::shared_ptr<process::ProcessManager> process_manager_;
    std::shared_ptr<streaming::StreamManager> stream_manager_;  // 流管理器（可选）
    
    // 通用辅助类
    std::unique_ptr<gateway::utils::SmartStreamProcessor> smart_processor_;  // 智能流处理器
    std::unique_ptr<gateway::utils::FFmpegProcessHelper> ffmpeg_helper_;  // FFmpeg 进程管理辅助
    std::unique_ptr<gateway::utils::BitrateAllocationHelper> bitrate_helper_;  // 码率分配辅助
    
    std::map<std::string, StreamInfo> streams_;  // stream_id -> StreamInfo
    std::mutex streams_mutex_;
    
    // FFmpeg 路径
    std::string ffmpeg_path_;
    std::string ffprobe_path_;
    std::string zlm_rtsp_url_; // 用于推流到 ZLM

    // 统一 FFmpeg 命令构建器
    std::unique_ptr<gateway::utils::FFmpegCommandBuilder> ffmpeg_builder_;
};

} // namespace gateway

#endif // GATEWAY_HTTPFLV_HTTPFLV_GATEWAY_HPP

