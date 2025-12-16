#ifndef UTILS_FFMPEG_LOG_ANALYZER_HPP
#define UTILS_FFMPEG_LOG_ANALYZER_HPP

#include <string>

namespace utils {

/**
 * @brief FFmpeg 日志分析工具类
 * 
 * 统一封装 FFmpeg 日志文件的错误检测逻辑，减少重复代码
 */
class FFmpegLogAnalyzer {
public:
    /**
     * @brief 分析日志文件，检测是否有错误
     * @param log_file 日志文件路径
     * @param max_lines 读取的最大行数（默认30行，从文件末尾读取）
     * @return pair<bool, bool>：第一个bool表示是否有错误，第二个bool表示是否有成功指标
     */
    static std::pair<bool, bool> AnalyzeLogFile(const std::string& log_file, int max_lines = 30);

    /**
     * @brief 检查日志内容中是否有错误关键词
     * @param log_content 日志内容（已转换为小写）
     * @param protocol_hint 协议提示（如 "quic"），用于特定协议的错误检测
     * @return pair<bool, bool>：第一个bool表示是否有错误，第二个bool表示是否有成功指标
     */
    static std::pair<bool, bool> AnalyzeLogContent(const std::string& log_content, 
                                                   const std::string& protocol_hint = "");

private:
    /**
     * @brief 检查是否有成功指标
     */
    static bool HasSuccessIndicators(const std::string& log_content);

    /**
     * @brief 检查是否有错误关键词
     */
    static bool HasErrorKeywords(const std::string& log_content, const std::string& protocol_hint);
};

} // namespace utils

#endif // UTILS_FFMPEG_LOG_ANALYZER_HPP

