#ifndef GATEWAY_UTILS_FFMPEG_COMMAND_BUILDER_HPP
#define GATEWAY_UTILS_FFMPEG_COMMAND_BUILDER_HPP

#include "config/config_loader.hpp"
#include "gateway/utils/stream_info_detector.hpp"
#include <string>
#include <memory>
#include <vector>

namespace gateway {
namespace utils {

/**
 * @brief FFmpeg 命令构建选项
 */
struct FFmpegCommandOptions {
    // 输入参数
    std::string input_url;              // 输入 URL (RTSP/RTMP/HTTP) 或设备 ID
    std::string input_format;           // 输入格式 (avfoundation, v4l2, flv, etc.)
    int input_fps = 0;                  // 输入帧率 (用于 device input)
    int input_width = 0;                // 输入宽 (用于 device input)
    int input_height = 0;               // 输入高 (用于 device input)
    int audio_device_index = -1;        // 音频设备索引 (用于 device input)
    std::string audio_input_format;     // 音频输入格式 (如 :default, :1)

    // 输出参数
    std::string target_app;
    std::string target_stream;
    std::string output_protocol;        // 目标协议 (webrtc, http-flv, hls, etc.)
    std::string gateway_name;           // Gateway 名称 (用于日志和 URL 生成)
    
    // 转码控制
    int target_bitrate_kbps = 0;        // 目标码率 (0 = auto)
    bool use_native_resolution = true;  // 是否优先使用原生分辨率
    
    // 流信息 (用于智能决策)
    StreamInfoResult stream_info;
};

/**
 * @brief 统一的 FFmpeg 命令构建器
 * 
 * 集中管理所有 Gateway 的 FFmpeg 命令生成逻辑，消除重复代码。
 * 支持：
 * 1. 本地摄像头输入 (macOS/Linux)
 * 2. 网络流输入 (RTSP/RTMP/HTTP)
 * 3. 智能转码决策 (Copy vs Transcode)
 * 4. WebRTC 专用参数优化
 * 5. 智能分辨率和码率控制
 */
class FFmpegCommandBuilder {
public:
    FFmpegCommandBuilder(std::shared_ptr<config::Config> config,
                         const std::string& ffmpeg_path,
                         const std::string& zlm_rtmp_url);

    /**
     * @brief 构建 FFmpeg 命令
     */
    std::string BuildCommand(const FFmpegCommandOptions& options) const;

private:
    // 构建输入部分
    void BuildInput(std::ostringstream& oss, const FFmpegCommandOptions& options) const;
    void BuildDeviceInput(std::ostringstream& oss, const FFmpegCommandOptions& options) const;
    void BuildNetworkInput(std::ostringstream& oss, const FFmpegCommandOptions& options) const;

    // 构建编码/转码部分
    void BuildEncoding(std::ostringstream& oss, const FFmpegCommandOptions& options) const;
    void BuildWebRTCEncoding(std::ostringstream& oss, const FFmpegCommandOptions& options) const;
    void BuildStreamingEncoding(std::ostringstream& oss, const FFmpegCommandOptions& options) const;
    void BuildDefaultEncoding(std::ostringstream& oss, const FFmpegCommandOptions& options) const;

    // 构建输出部分
    void BuildOutput(std::ostringstream& oss, const FFmpegCommandOptions& options) const;

    // 辅助方法
    std::pair<int, int> SelectResolution(const std::string& config_resolution, 
                                        int native_width, int native_height) const;

private:
    std::shared_ptr<config::Config> config_;
    std::string ffmpeg_path_;
    std::string zlm_rtmp_url_;
};

} // namespace utils
} // namespace gateway

#endif // GATEWAY_UTILS_FFMPEG_COMMAND_BUILDER_HPP
