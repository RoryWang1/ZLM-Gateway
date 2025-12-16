#ifndef UTILS_AUDIO_CODEC_DETECTOR_HPP
#define UTILS_AUDIO_CODEC_DETECTOR_HPP

#include <string>

namespace utils {

/**
 * @brief 音频编解码器检测工具类
 */
class AudioCodecDetector {
public:
    /**
     * @brief 检测流的音频编解码器
     * @param url 流URL
     * @param ffprobe_path FFprobe 可执行文件路径（可选，如果为空则使用默认路径）
     * @param timeout_seconds 超时时间（秒），默认5秒
     * @return 编解码器名称（如 "aac", "opus", "pcm_alaw" 等），如果检测失败返回空字符串
     */
    static std::string DetectAudioCodec(
        const std::string& url,
        const std::string& ffprobe_path = "",
        int timeout_seconds = 5);
};

} // namespace utils

#endif // UTILS_AUDIO_CODEC_DETECTOR_HPP

