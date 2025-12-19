#ifndef PROCESS_FFPROBE_DETECTOR_HPP
#define PROCESS_FFPROBE_DETECTOR_HPP

#include <string>
#include <vector>
#include <map>

namespace process {

/**
 * @brief 流编码信息
 */
struct StreamCodecInfo {
    std::string codec_name;      // 编码器名称（如 "h264", "aac"）
    std::string codec_type;      // 编码类型（"video", "audio"）
    int width = 0;               // 视频宽度
    int height = 0;              // 视频高度
    int bitrate = 0;             // 码率（bps）
    double fps = 0.0;            // 帧率
    std::string pixel_format;    // 像素格式（视频）
    std::string profile;         // Profile (Baseline, Main, High)
    std::string level;           // Level (3.1, 4.0, etc.)
    int sample_rate = 0;         // 采样率（音频）
    int channels = 0;            // 声道数（音频）
};

/**
 * @brief 流信息
 */
struct StreamInfo {
    std::string format_name;     // 容器格式（如 "flv", "rtsp"）
    double duration = 0.0;       // 时长（秒）
    int64_t size = 0;            // 文件大小（字节）
    int total_bitrate = 0;       // 总码率（bps，从 format 级别获取）
    std::vector<StreamCodecInfo> codecs;  // 编码信息列表
};

/**
 * @brief FFprobe 智能参数检测器
 * 
 * 功能：
 * 1. 使用 FFprobe 检测源流参数
 * 2. 自动选择编码参数（-c copy 或转码）
 * 3. 性能优化建议
 */
class FFprobeDetector {
public:
    /**
     * @brief 构造函数
     * @param ffprobe_path FFprobe 路径（空则使用系统 PATH 中的 ffprobe）
     */
    explicit FFprobeDetector(const std::string& ffprobe_path = "");

    /**
     * @brief 检测流信息
     * @param source_url 源流 URL
     * @param timeout_seconds 超时时间（秒，默认 10 秒）
     * @return 流信息，失败返回空对象（format_name 为空）
     */
    StreamInfo DetectStreamInfo(const std::string& source_url, int timeout_seconds = 10);

    /**
     * @brief 检测流信息（带认证）
     * @param source_url 源流 URL
     * @param username 用户名
     * @param password 密码
     * @param timeout_seconds 超时时间（秒）
     * @return 流信息
     */
    StreamInfo DetectStreamInfo(const std::string& source_url,
                                const std::string& username,
                                const std::string& password,
                                int timeout_seconds = 10);

    /**
     * @brief 生成优化的 FFmpeg 参数
     * @param stream_info 流信息
     * @param target_protocol 目标协议（"rtsp", "rtmp" 等）
     * @return FFmpeg 参数列表
     */
    std::vector<std::string> GenerateOptimizedParams(const StreamInfo& stream_info,
                                                     const std::string& target_protocol);

    /**
     * @brief 检查是否可以使用 -c copy（编码兼容）
     * @param stream_info 流信息
     * @param target_protocol 目标协议
     * @return 是否可以使用 copy
     */
    bool CanUseCopy(const StreamInfo& stream_info, const std::string& target_protocol);

private:
    /**
     * @brief 执行 FFprobe 命令
     * @param args FFprobe 参数
     * @param timeout_seconds 超时时间
     * @return 输出内容
     */
    std::string ExecuteFFprobe(const std::vector<std::string>& args, int timeout_seconds);

    /**
     * @brief 解析 JSON 输出
     * @param json_output JSON 字符串
     * @return 流信息
     */
    StreamInfo ParseJSONOutput(const std::string& json_output);

    /**
     * @brief 检查编码兼容性
     * @param codec_name 编码器名称
     * @param target_protocol 目标协议
     * @return 是否兼容
     */
    bool IsCodecCompatible(const std::string& codec_name, const std::string& target_protocol);

    std::string ffprobe_path_;
};

} // namespace process

#endif // PROCESS_FFPROBE_DETECTOR_HPP

