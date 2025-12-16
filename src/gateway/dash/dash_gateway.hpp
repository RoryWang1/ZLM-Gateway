#ifndef GATEWAY_DASH_DASH_GATEWAY_HPP
#define GATEWAY_DASH_DASH_GATEWAY_HPP

#include "gateway/base/gateway_base.hpp"
#include "gateway/utils/smart_stream_processor.hpp"
#include "gateway/utils/ffmpeg_process_helper.hpp"
#include "gateway/utils/bitrate_allocation_helper.hpp"
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
}


namespace streaming {
class StreamManager;
}

namespace utils {
class BitrateAllocator;
}

namespace gateway {

/**
 * @brief DASH Gateway
 * 
 * 使用 FFmpeg 从 DASH 源拉流并推送到 ZLMediaKit
 */
class DASHGateway : public GatewayBase {
public:
    /**
     * @brief 构造函数
     * @param config 配置对象
     * @param zlm_client ZLMediaKit 客户端
     * @param process_manager 进程管理器
     * @param stream_manager 流管理器（可选）
     * @param bitrate_allocator 码率分配器（可选）
     */
    DASHGateway(std::shared_ptr<config::Config> config,
                std::shared_ptr<streaming::ZLMClient> zlm_client,
                std::shared_ptr<process::ProcessManager> process_manager = nullptr,
                std::shared_ptr<streaming::StreamManager> stream_manager = nullptr,
                std::shared_ptr<::utils::BitrateAllocator> bitrate_allocator = nullptr);

    /**
     * @brief 析构函数
     */
    ~DASHGateway();

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

    std::string GetProtocol() const override { return "dash"; }

private:
    /**
     * @brief 流信息
     */
    struct StreamInfo {
        std::string source_url;
        std::string target_app;
        std::string target_stream;
        std::string output_protocol; // 输出协议
        int pid = 0;  // FFmpeg 进程 PID
        GatewayStatus status = GatewayStatus::Stopped;
    };

    /**
     * @brief 构建 FFmpeg 命令
     * @param info 流信息
     * @param bitrate_kbps 码率（kbps，0表示使用默认值或智能分配）
     * @return FFmpeg 命令字符串
     */
    std::string BuildFFmpegCommand(const StreamInfo& info, 
                                   const gateway::utils::StreamInfoResult& stream_info_result,
                                   int bitrate_kbps = 0);

    /**
     * @brief 使用 FFprobe 检测流参数并优化 FFmpeg 命令
     * @param info 流信息
     * @param stream_info_result 流信息检测结果
     * @param bitrate_kbps 码率（kbps，0表示使用默认值或智能分配）
     * @return 优化后的 FFmpeg 命令字符串
     */
    std::string BuildOptimizedFFmpegCommand(const StreamInfo& info, 
                                           const gateway::utils::StreamInfoResult& stream_info_result,
                                           int bitrate_kbps = 0);


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
    std::string ffmpeg_path_;
    std::string zlm_rtsp_url_;  // ZLMediaKit RTSP 推流地址
};

} // namespace gateway

#endif // GATEWAY_DASH_DASH_GATEWAY_HPP

