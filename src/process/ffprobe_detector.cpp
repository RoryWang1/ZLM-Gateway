#include "process/ffprobe_detector.hpp"
#include "utils/logger.hpp"
#include <sstream>
#include <fstream>
#include <algorithm>
#include <unistd.h>
#include <sys/wait.h>
#include <signal.h>
#include <fcntl.h>
#include <errno.h>
#include <cstring>
#include <nlohmann/json.hpp>

namespace process {

FFprobeDetector::FFprobeDetector(const std::string& ffprobe_path) {
    if (ffprobe_path.empty()) {
        ffprobe_path_ = "ffprobe";  // 默认使用系统 PATH 中的 ffprobe
    } else {
        ffprobe_path_ = ffprobe_path;
    }
}

StreamInfo FFprobeDetector::DetectStreamInfo(const std::string& source_url, int timeout_seconds) {
    return DetectStreamInfo(source_url, "", "", timeout_seconds);
}

StreamInfo FFprobeDetector::DetectStreamInfo(const std::string& source_url,
                                             const std::string& username,
                                             const std::string& password,
                                             int timeout_seconds) {
    // 构建 FFprobe 命令参数
    std::vector<std::string> args;
    args.push_back("-v");
    args.push_back("quiet");
    args.push_back("-print_format");
    args.push_back("json");
    args.push_back("-show_format");
    args.push_back("-show_streams");
    
    // 如果提供了认证信息，添加到 URL
    std::string url = source_url;
    if (!username.empty() && !password.empty()) {
        // 在 URL 中添加认证信息
        size_t pos = url.find("://");
        if (pos != std::string::npos) {
            std::string protocol = url.substr(0, pos + 3);
            std::string rest = url.substr(pos + 3);
            url = protocol + username + ":" + password + "@" + rest;
        }
    }
    
    args.push_back(url);
    
    // 执行 FFprobe
    std::string json_output = ExecuteFFprobe(args, timeout_seconds);
    if (json_output.empty()) {
        // ffprobe 失败通常是源 URL 不存在或无法访问，这是正常情况，降级为调试日志
        ::utils::Logger::Get()->debug("FFprobe 执行失败或超时: {} (将使用默认参数)", source_url);
        return StreamInfo();
    }
    
    // 解析 JSON 输出
    return ParseJSONOutput(json_output);
}

std::vector<std::string> FFprobeDetector::GenerateOptimizedParams(const StreamInfo& stream_info,
                                                                  const std::string& target_protocol) {
    std::vector<std::string> params;
    
    if (stream_info.format_name.empty()) {
        // 无法检测流信息，使用默认参数
        return params;
    }
    
    // 检查是否可以使用 -c copy
    if (CanUseCopy(stream_info, target_protocol)) {
        // 使用 copy，性能最优
        params.push_back("-c");
        params.push_back("copy");
        LOG_DEBUG("使用 -c copy（编码兼容）");
    } else {
        // 需要转码，保持原始编码参数
        for (const auto& codec : stream_info.codecs) {
            if (codec.codec_type == "video") {
                params.push_back("-c:v");
                params.push_back(codec.codec_name);
                if (codec.bitrate > 0) {
                    params.push_back("-b:v");
                    params.push_back(std::to_string(codec.bitrate));
                }
                if (codec.fps > 0) {
                    params.push_back("-r");
                    params.push_back(std::to_string(static_cast<int>(codec.fps)));
                }
            } else if (codec.codec_type == "audio") {
                params.push_back("-c:a");
                params.push_back(codec.codec_name);
                if (codec.bitrate > 0) {
                    params.push_back("-b:a");
                    params.push_back(std::to_string(codec.bitrate));
                }
            }
        }
        LOG_DEBUG("使用转码（编码不兼容）");
    }
    
    return params;
}

bool FFprobeDetector::CanUseCopy(const StreamInfo& stream_info, const std::string& target_protocol) {
    // 检查所有编码是否兼容目标协议
    for (const auto& codec : stream_info.codecs) {
        if (!IsCodecCompatible(codec.codec_name, target_protocol)) {
            return false;
        }
    }
    return true;
}

std::string FFprobeDetector::ExecuteFFprobe(const std::vector<std::string>& args, int timeout_seconds) {
    // 创建管道
    int pipefd[2];
    if (pipe(pipefd) == -1) {
        LOG_ERROR("创建管道失败: {}", strerror(errno));
        return "";
    }
    
    pid_t pid = fork();
    if (pid < 0) {
        LOG_ERROR("fork 失败: {}", strerror(errno));
        close(pipefd[0]);
        close(pipefd[1]);
        return "";
    } else if (pid == 0) {
        // 子进程：执行 FFprobe
        close(pipefd[0]);  // 关闭读端
        
        // 重定向 stdout 到管道
        dup2(pipefd[1], STDOUT_FILENO);
        dup2(pipefd[1], STDERR_FILENO);
        close(pipefd[1]);
        
        // 构建参数数组
        std::vector<const char*> argv;
        argv.push_back(ffprobe_path_.c_str());
        for (const auto& arg : args) {
            argv.push_back(arg.c_str());
        }
        argv.push_back(nullptr);
        
        // 执行 FFprobe
        execvp(ffprobe_path_.c_str(), const_cast<char* const*>(argv.data()));
        exit(1);  // 如果 exec 失败
    } else {
        // 父进程：读取输出
        close(pipefd[1]);  // 关闭写端
        
        std::string output;
        char buffer[4096];
        
        // 设置超时
        fd_set readfds;
        struct timeval timeout;
        timeout.tv_sec = timeout_seconds;
        timeout.tv_usec = 0;
        
        while (true) {
            FD_ZERO(&readfds);
            FD_SET(pipefd[0], &readfds);
            
            int ret = select(pipefd[0] + 1, &readfds, nullptr, nullptr, &timeout);
            if (ret == 0) {
                // 超时
                LOG_WARN("FFprobe 执行超时");
                kill(pid, SIGKILL);
                waitpid(pid, nullptr, 0);
                close(pipefd[0]);
                return "";
            } else if (ret < 0) {
                LOG_ERROR("select 失败: {}", strerror(errno));
                kill(pid, SIGKILL);
                waitpid(pid, nullptr, 0);
                close(pipefd[0]);
                return "";
            }
            
            if (FD_ISSET(pipefd[0], &readfds)) {
                ssize_t n = read(pipefd[0], buffer, sizeof(buffer) - 1);
                if (n <= 0) {
                    break;  // EOF 或错误
                }
                buffer[n] = '\0';
                output += buffer;
            }
        }
        
        close(pipefd[0]);
        
        // 等待子进程退出
        int status;
        waitpid(pid, &status, 0);
        
        if (WIFEXITED(status) && WEXITSTATUS(status) == 0) {
            return output;
        } else {
            // ffprobe 失败通常是源 URL 不存在或无法访问，这是正常情况，降级为警告
            int exit_code = WIFEXITED(status) ? WEXITSTATUS(status) : -1;
            ::utils::Logger::Get()->warn("ffprobe 执行失败，退出码: {} (源可能不存在或无法访问，将使用默认参数)", exit_code);
            return "";
        }
    }
}

StreamInfo FFprobeDetector::ParseJSONOutput(const std::string& json_output) {
    StreamInfo info;
    
    try {
        auto j = nlohmann::json::parse(json_output);
        
        // 解析 format 信息
        int format_bitrate = 0;
        if (j.contains("format")) {
            const auto& format = j["format"];
            if (format.contains("format_name")) {
                info.format_name = format["format_name"].get<std::string>();
            }
            if (format.contains("duration")) {
                info.duration = std::stod(format["duration"].get<std::string>());
            }
            if (format.contains("size")) {
                info.size = std::stoll(format["size"].get<std::string>());
            }
            // 从 format 级别获取总码率（如果 stream 级别没有）
            if (format.contains("bit_rate")) {
                std::string bitrate_str = format["bit_rate"].get<std::string>();
                format_bitrate = std::stoi(bitrate_str);
                info.total_bitrate = format_bitrate;  // 保存到 StreamInfo
            }
        }
        
        // 解析 streams 信息
        if (j.contains("streams")) {
            for (const auto& stream : j["streams"]) {
                StreamCodecInfo codec;
                
                if (stream.contains("codec_name")) {
                    codec.codec_name = stream["codec_name"].get<std::string>();
                }
                if (stream.contains("codec_type")) {
                    codec.codec_type = stream["codec_type"].get<std::string>();
                }
                if (stream.contains("width")) {
                    codec.width = stream["width"].get<int>();
                }
                if (stream.contains("height")) {
                    codec.height = stream["height"].get<int>();
                }
                if (stream.contains("bit_rate")) {
                    std::string bitrate_str = stream["bit_rate"].get<std::string>();
                    codec.bitrate = std::stoi(bitrate_str);
                }
                if (stream.contains("r_frame_rate")) {
                    std::string fps_str = stream["r_frame_rate"].get<std::string>();
                    // 解析分数格式 "30/1"
                    size_t pos = fps_str.find('/');
                    if (pos != std::string::npos) {
                        double num = std::stod(fps_str.substr(0, pos));
                        double den = std::stod(fps_str.substr(pos + 1));
                        if (den > 0) {
                            codec.fps = num / den;
                        }
                    }
                }
                if (stream.contains("pix_fmt")) {
                    codec.pixel_format = stream["pix_fmt"].get<std::string>();
                }
                if (stream.contains("sample_rate")) {
                    std::string sample_rate_str = stream["sample_rate"].get<std::string>();
                    codec.sample_rate = std::stoi(sample_rate_str);
                }
                if (stream.contains("channels")) {
                    codec.channels = stream["channels"].get<int>();
                }
                
                info.codecs.push_back(codec);
            }
        }
    } catch (const nlohmann::json::exception& e) {
        LOG_ERROR("解析 FFprobe JSON 输出失败: {}", e.what());
        return StreamInfo();
    }
    
    return info;
}

bool FFprobeDetector::IsCodecCompatible(const std::string& codec_name, const std::string& target_protocol) {
    // RTSP/RTMP 通常支持 H.264/H.265 和 AAC
    if (target_protocol == "rtsp" || target_protocol == "rtmp") {
        std::string lower_codec = codec_name;
        std::transform(lower_codec.begin(), lower_codec.end(), lower_codec.begin(), ::tolower);
        
        // 视频编码
        if (lower_codec == "h264" || lower_codec == "libx264" ||
            lower_codec == "h265" || lower_codec == "hevc" || lower_codec == "libx265") {
            return true;
        }
        
        // 音频编码
        if (lower_codec == "aac" || lower_codec == "libfdk_aac" ||
            lower_codec == "mp3" || lower_codec == "libmp3lame") {
            return true;
        }
    }
    
    // 默认认为不兼容，需要转码
    return false;
}

} // namespace process

