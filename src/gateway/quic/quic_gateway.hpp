#ifndef GATEWAY_QUIC_QUIC_GATEWAY_HPP
#define GATEWAY_QUIC_QUIC_GATEWAY_HPP

#include "gateway/base/gateway_base.hpp"
#include "gateway/utils/smart_stream_processor.hpp"
#include "gateway/utils/ffmpeg_process_helper.hpp"
#include "gateway/utils/bitrate_allocation_helper.hpp"
#include "gateway/quic/quic_server.hpp"
#include "gateway/quic/fec_processor.hpp"
#include "gateway/quic/ts_parser.hpp"
#include "gateway/quic/ts_to_ffmpeg_bridge.hpp"
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
 * @brief QUIC Gateway
 * 
 * 使用 FFmpeg 从 QUIC 源拉流并推送到 ZLMediaKit
 */
class QUICGateway : public GatewayBase {
public:
    /**
     * @brief 构造函数
     * @param config 配置对象
     * @param zlm_client ZLMediaKit 客户端
     * @param process_manager 进程管理器（可选，如果提供则使用统一的进程管理）
     * @param stream_manager 流管理器（可选）
     * @param bitrate_allocator 码率分配器（可选）
     */
    QUICGateway(std::shared_ptr<config::Config> config,
                std::shared_ptr<streaming::ZLMClient> zlm_client,
                std::shared_ptr<process::ProcessManager> process_manager = nullptr,
                std::shared_ptr<streaming::StreamManager> stream_manager = nullptr,
                std::shared_ptr<::utils::BitrateAllocator> bitrate_allocator = nullptr);

    /**
     * @brief 析构函数
     */
    ~QUICGateway();

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

    std::string GetProtocol() const override { return "quic"; }

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
        
        // QUIC 服务器模式相关（如果使用）
        std::shared_ptr<quic::QuicConnection> quic_connection;
        std::unique_ptr<quic::FECProcessor> fec_processor;
        std::unique_ptr<quic::TSParser> ts_parser;
        std::unique_ptr<quic::TSToFFmpegBridge> ffmpeg_bridge;
        bool use_quic_server_mode = false;  // 是否使用 QUIC 服务器模式
    };
    
    /**
     * @brief 解析 source_url，判断是否使用 QUIC 服务器模式
     * @param source_url 源 URL
     * @return true 表示应该使用 QUIC 服务器模式（格式：quic://host:port/stream_id）
     *         false 表示使用 FFmpeg 拉流模式
     */
    bool ShouldUseQuicServerMode(const std::string& source_url) const;
    
    /**
     * @brief 启动 QUIC 服务器模式的流
     * @param info 流信息
     * @return 是否成功
     */
    bool StartQuicServerMode(StreamInfo& info);
    
    /**
     * @brief 停止 QUIC 服务器模式的流
     * @param info 流信息
     * @return 是否成功
     */
    bool StopQuicServerMode(StreamInfo& info);

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

    /**
     * @brief 生成流的唯一标识符
     * @param app 应用名
     * @param stream 流名
     * @return 唯一标识符
     */
    std::string GenerateStreamId(const std::string& app, const std::string& stream) const {
        return app + "/" + stream;
    }


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
    
    // QUIC 服务器（共享实例，用于所有 QUIC 服务器模式的流）
    std::unique_ptr<quic::QuicServer> quic_server_;
    std::mutex quic_server_mutex_;
    bool quic_server_started_;
}; // class QUICGateway

} // namespace gateway

#endif // GATEWAY_QUIC_QUIC_GATEWAY_HPP

