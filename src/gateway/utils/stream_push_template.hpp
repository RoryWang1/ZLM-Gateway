#ifndef GATEWAY_UTILS_STREAM_PUSH_TEMPLATE_HPP
#define GATEWAY_UTILS_STREAM_PUSH_TEMPLATE_HPP

#include "gateway/utils/source_descriptor.hpp"
#include "gateway/utils/stream_start_validator.hpp"
#include "gateway/base/gateway_base.hpp"
#include "streaming/zlmediakit/zlm_client.hpp"
#include "streaming/stream_manager.hpp"
#include "process/process_manager.hpp"
#include <string>
#include <memory>
#include <functional>

namespace gateway {
namespace utils {

/**
 * @brief 推流结果
 */
struct PushStreamResult {
    bool success = false;
    std::string error_code;      // 错误码（如 "SOURCE_NOT_FOUND", "CODEC_UNSUPPORTED"）
    std::string error_message;    // 错误消息
    int pid = 0;                  // 进程ID（如果使用 FFmpeg）
    GatewayStatus status = GatewayStatus::Stopped;
};

/**
 * @brief 通用推流模板
 * 
 * 提供统一的推流流程，支持多种源类型（URL、本地摄像头、设备等）
 * 根据源类型自动选择推流方式（ZLM API 或 FFmpeg）
 */
class StreamPushTemplate {
public:
    /**
     * @brief 构造函数
     * @param zlm_client ZLM 客户端
     * @param stream_manager 流管理器
     * @param process_manager 进程管理器（可选）
     */
    StreamPushTemplate(std::shared_ptr<streaming::ZLMClient> zlm_client,
                      std::shared_ptr<streaming::StreamManager> stream_manager,
                      std::shared_ptr<process::ProcessManager> process_manager = nullptr);

    /**
     * @brief 启动推流
     * @param source_desc 源描述
     * @param target_app 目标应用名
     * @param target_stream 目标流名
     * @param output_protocol 输出协议（http-flv, hls, webrtc 等）
     * @param gateway_type Gateway 类型（用于日志和状态管理）
     * @param build_ffmpeg_command_callback 构建 FFmpeg 命令的回调（用于需要 FFmpeg 的场景）
     * @return 推流结果
     */
    PushStreamResult PushStream(
        const SourceDescriptor& source_desc,
        const std::string& target_app,
        const std::string& target_stream,
        const std::string& output_protocol,
        const std::string& gateway_type,
        std::function<std::string()> build_ffmpeg_command_callback = nullptr);

    /**
     * @brief 停止推流
     * @param target_app 目标应用名
     * @param target_stream 目标流名
     * @param pid 进程ID（如果使用 FFmpeg）
     * @param gateway_type Gateway 类型
     * @return 是否成功
     */
    bool StopStream(const std::string& target_app,
                   const std::string& target_stream,
                   int pid,
                   const std::string& gateway_type);

private:
    /**
     * @brief 使用 ZLM API 推流（原生协议）
     */
    PushStreamResult PushWithZLMAPI(const SourceDescriptor& source_desc,
                                   const std::string& target_app,
                                   const std::string& target_stream,
                                   const std::string& output_protocol,
                                   const std::string& gateway_type);

    /**
     * @brief 使用 FFmpeg 推流（转码）
     */
    PushStreamResult PushWithFFmpeg(const SourceDescriptor& source_desc,
                                    const std::string& target_app,
                                    const std::string& target_stream,
                                    const std::string& output_protocol,
                                    const std::string& gateway_type,
                                    std::function<std::string()> build_command_callback);

    /**
     * @brief 判断是否应该使用 FFmpeg 转码
     */
    bool ShouldUseFFmpeg(const SourceDescriptor& source_desc,
                        const std::string& output_protocol) const;

    /**
     * @brief 映射错误到统一错误码
     */
    void MapError(const std::string& error_context,
                 PushStreamResult& result) const;

private:
    std::shared_ptr<streaming::ZLMClient> zlm_client_;
    std::shared_ptr<streaming::StreamManager> stream_manager_;
    std::shared_ptr<process::ProcessManager> process_manager_;
    std::unique_ptr<StreamStartValidator> validator_;
};

} // namespace utils
} // namespace gateway

#endif // GATEWAY_UTILS_STREAM_PUSH_TEMPLATE_HPP

