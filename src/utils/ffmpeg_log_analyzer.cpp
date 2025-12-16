#include "utils/ffmpeg_log_analyzer.hpp"
#include <fstream>
#include <algorithm>
#include <sstream>

namespace utils {

std::pair<bool, bool> FFmpegLogAnalyzer::AnalyzeLogFile(const std::string& log_file, int max_lines) {
    std::ifstream log_stream(log_file);
    if (!log_stream.is_open()) {
        return {false, false};  // 文件无法打开，无法判断
    }

    std::string line, last_lines;
    int line_count = 0;
    
    // 读取最后 max_lines 行
    while (std::getline(log_stream, line)) {
        last_lines += line + "\n";
        if (++line_count > max_lines) {
            // 移除第一行，保持只保留最后 max_lines 行
            size_t pos = last_lines.find('\n');
            if (pos != std::string::npos) {
                last_lines = last_lines.substr(pos + 1);
            }
        }
    }

    if (last_lines.empty()) {
        return {false, false};
    }

    // 转换为小写进行分析
    std::string lower_log = last_lines;
    std::transform(lower_log.begin(), lower_log.end(), lower_log.begin(), ::tolower);

    return AnalyzeLogContent(lower_log);
}

std::pair<bool, bool> FFmpegLogAnalyzer::AnalyzeLogContent(const std::string& log_content, 
                                                           const std::string& protocol_hint) {
    bool has_success = HasSuccessIndicators(log_content);
    bool has_error = HasErrorKeywords(log_content, protocol_hint);

    return {has_error, has_success};
}

bool FFmpegLogAnalyzer::HasSuccessIndicators(const std::string& log_content) {
    // 检查成功指标
    return log_content.find("stream #0") != std::string::npos ||
           log_content.find("output #0") != std::string::npos ||
           log_content.find("stream mapping") != std::string::npos ||
           log_content.find("frame=") != std::string::npos ||
           log_content.find("fps=") != std::string::npos ||
           log_content.find("duration:") != std::string::npos ||
           log_content.find("bitrate:") != std::string::npos;
}

bool FFmpegLogAnalyzer::HasErrorKeywords(const std::string& log_content, const std::string& protocol_hint) {
    // 通用错误关键词
    if (log_content.find("connection refused") != std::string::npos ||
        log_content.find("operation timed out") != std::string::npos ||
        log_content.find("error opening input file") != std::string::npos ||
        log_content.find("server returned 404") != std::string::npos ||
        log_content.find("server returned 500") != std::string::npos ||
        log_content.find("failed to connect") != std::string::npos ||
        log_content.find("unable to open") != std::string::npos ||
        log_content.find("invalid data found") != std::string::npos) {
        return true;
    }

    // 协议特定的错误检测
    if (!protocol_hint.empty()) {
        std::string lower_hint = protocol_hint;
        std::transform(lower_hint.begin(), lower_hint.end(), lower_hint.begin(), ::tolower);
        
        if (log_content.find(lower_hint) != std::string::npos) {
            if (log_content.find("error") != std::string::npos || 
                log_content.find("failed") != std::string::npos) {
                return true;
            }
        }
    }

    return false;
}

} // namespace utils

