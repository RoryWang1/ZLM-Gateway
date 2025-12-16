#include "utils/audio_codec_detector.hpp"
#include "utils/logger.hpp"
#include "utils/ffmpeg_path.hpp"
#include <nlohmann/json.hpp>
#include <unistd.h>
#include <sys/wait.h>
#include <sys/select.h>
#include <sys/time.h>
#include <signal.h>
#include <fcntl.h>
#include <errno.h>
#include <cstring>
#include <algorithm>

namespace utils {

std::string AudioCodecDetector::DetectAudioCodec(
    const std::string& url,
    const std::string& ffprobe_path,
    int timeout_seconds) {
    
    // 确定 FFprobe 路径
    std::string ffprobe_cmd = ffprobe_path.empty() 
        ? utils::GetFFprobePath()
        : ffprobe_path;
    
    if (ffprobe_cmd.empty()) {
        LOG_WARN("FFprobe 路径未配置，无法检测音频编解码器");
        return "";
    }
    
    // 构建 ffprobe 命令
    // 注意：使用 -v error 而不是 -v quiet，这样可以在 stderr 中看到错误信息（如 404）
    std::string command = ffprobe_cmd + 
        " -v error -print_format json -show_streams -select_streams a:0 " +
        url;
    
    // 创建两个管道：一个用于 stdout，一个用于 stderr
    int stdout_pipe[2], stderr_pipe[2];
    if (pipe(stdout_pipe) < 0 || pipe(stderr_pipe) < 0) {
        LOG_WARN("创建管道失败: {}", strerror(errno));
        if (pipe(stdout_pipe) >= 0) {
            close(stdout_pipe[0]);
            close(stdout_pipe[1]);
        }
        if (pipe(stderr_pipe) >= 0) {
            close(stderr_pipe[0]);
            close(stderr_pipe[1]);
        }
        return "";
    }
    
    // 设置管道为非阻塞
    fcntl(stdout_pipe[0], F_SETFL, O_NONBLOCK);
    fcntl(stderr_pipe[0], F_SETFL, O_NONBLOCK);
    
    // fork 子进程执行 ffprobe
    pid_t pid = fork();
    if (pid < 0) {
        LOG_WARN("fork 失败: {}", strerror(errno));
        close(stdout_pipe[0]);
        close(stdout_pipe[1]);
        close(stderr_pipe[0]);
        close(stderr_pipe[1]);
        return "";
    }
    
    if (pid == 0) {
        // 子进程
        close(stdout_pipe[0]);
        close(stderr_pipe[0]);
        dup2(stdout_pipe[1], STDOUT_FILENO);
        dup2(stderr_pipe[1], STDERR_FILENO);
        close(stdout_pipe[1]);
        close(stderr_pipe[1]);
        
        // 执行 ffprobe
        execl("/bin/sh", "sh", "-c", command.c_str(), nullptr);
        _exit(1);
    }
    
    // 父进程
    close(stdout_pipe[1]);
    close(stderr_pipe[1]);
    
    std::string output;
    std::string error_output;
    char buffer[256];
    
    // 记录开始时间，用于计算剩余超时时间
    struct timeval start_time, current_time;
    gettimeofday(&start_time, nullptr);
    
    bool stdout_eof = false;
    bool stderr_eof = false;
    
    while (!stdout_eof || !stderr_eof) {
        // 计算剩余超时时间
        gettimeofday(&current_time, nullptr);
        long elapsed_sec = current_time.tv_sec - start_time.tv_sec;
        long elapsed_usec = current_time.tv_usec - start_time.tv_usec;
        if (elapsed_usec < 0) {
            elapsed_sec--;
            elapsed_usec += 1000000;
        }
        
        // 在超时之前，检查 stderr 是否已经有错误信息（如 404）
        // 如果已经有错误信息，立即返回，不要等到超时
        if (!error_output.empty()) {
            std::string lower_error = error_output;
            std::transform(lower_error.begin(), lower_error.end(), lower_error.begin(), ::tolower);
            if (lower_error.find("404") != std::string::npos ||
                lower_error.find("not found") != std::string::npos ||
                lower_error.find("server returned 404") != std::string::npos) {
                // 已经检测到 404 错误，立即 kill 进程并返回
                kill(pid, SIGKILL);
                waitpid(pid, nullptr, 0);
                close(stdout_pipe[0]);
                close(stderr_pipe[0]);
                LOG_WARN("ffprobe 检测失败：源流不存在 (404): {}", url);
                return "";
            }
        }
        
        if (elapsed_sec >= timeout_seconds) {
            // 超时
            // 如果已经有错误输出，记录错误信息；否则记录超时
            if (!error_output.empty()) {
                std::string lower_error = error_output;
                std::transform(lower_error.begin(), lower_error.end(), lower_error.begin(), ::tolower);
                if (lower_error.find("404") != std::string::npos ||
                    lower_error.find("not found") != std::string::npos ||
                    lower_error.find("server returned 404") != std::string::npos) {
                    LOG_WARN("ffprobe 检测失败：源流不存在 (404): {}", url);
                } else {
                    LOG_WARN("ffprobe 执行超时（{}秒），错误信息: {}", timeout_seconds, error_output);
                }
            } else {
                LOG_WARN("ffprobe 执行超时（{}秒）: {}", timeout_seconds, url);
            }
            kill(pid, SIGKILL);
            waitpid(pid, nullptr, 0);
            close(stdout_pipe[0]);
            close(stderr_pipe[0]);
            return "";
        }
        
        // 如果两个管道都 EOF，直接退出循环，等待子进程退出
        if (stdout_eof && stderr_eof) {
            break;
        }
        
        // 设置剩余超时时间
        struct timeval timeout;
        timeout.tv_sec = timeout_seconds - elapsed_sec;
        timeout.tv_usec = (1000000 - elapsed_usec) % 1000000;
        
        fd_set readfds;
        FD_ZERO(&readfds);
        if (!stdout_eof) {
            FD_SET(stdout_pipe[0], &readfds);
        }
        if (!stderr_eof) {
            FD_SET(stderr_pipe[0], &readfds);
        }
        
        int max_fd = std::max(stdout_pipe[0], stderr_pipe[0]);
        int ret = select(max_fd + 1, &readfds, nullptr, nullptr, &timeout);
        if (ret == 0) {
            // 超时（虽然已经在上面的时间检查中处理了，但这里作为双重检查）
            LOG_WARN("ffprobe 执行超时（{}秒）: {}", timeout_seconds, url);
            kill(pid, SIGKILL);
            waitpid(pid, nullptr, 0);
            close(stdout_pipe[0]);
            close(stderr_pipe[0]);
            return "";
        } else if (ret < 0) {
            if (errno == EINTR) {
                // 被信号中断，继续循环
                continue;
            }
            LOG_WARN("select 失败: {}", strerror(errno));
            kill(pid, SIGKILL);
            waitpid(pid, nullptr, 0);
            close(stdout_pipe[0]);
            close(stderr_pipe[0]);
            return "";
        }
        
        if (FD_ISSET(stdout_pipe[0], &readfds)) {
            ssize_t n = read(stdout_pipe[0], buffer, sizeof(buffer) - 1);
            if (n <= 0) {
                stdout_eof = true;
            } else {
                buffer[n] = '\0';
                output += buffer;
            }
        }
        
        if (FD_ISSET(stderr_pipe[0], &readfds)) {
            ssize_t n = read(stderr_pipe[0], buffer, sizeof(buffer) - 1);
            if (n <= 0) {
                stderr_eof = true;
            } else {
                buffer[n] = '\0';
                error_output += buffer;
            }
        }
    }
    
    close(stdout_pipe[0]);
    close(stderr_pipe[0]);
    
    // 等待子进程退出
    int status;
    waitpid(pid, &status, 0);
    
    if (WIFEXITED(status) && WEXITSTATUS(status) == 0) {
        try {
            auto j = nlohmann::json::parse(output);
            if (!j["streams"].empty()) {
                return j["streams"][0].value("codec_name", "");
            }
        } catch (const std::exception& e) {
            LOG_WARN("ffprobe json parse error: {}", e.what());
        }
    } else {
        // 检查错误输出，判断是否是源流不存在
        std::string lower_error = error_output;
        std::transform(lower_error.begin(), lower_error.end(), lower_error.begin(), ::tolower);
        
        if (lower_error.find("404") != std::string::npos ||
            lower_error.find("not found") != std::string::npos ||
            lower_error.find("server returned 404") != std::string::npos) {
            LOG_WARN("ffprobe 检测失败：源流不存在 (404): {}", url);
        } else if (lower_error.find("timeout") != std::string::npos ||
                   lower_error.find("timed out") != std::string::npos) {
            LOG_WARN("ffprobe 检测失败：网络超时: {}", url);
        } else {
            LOG_WARN("ffprobe 执行失败，退出码: {}, 错误信息: {}", WEXITSTATUS(status), 
                    error_output.empty() ? "(无错误信息)" : error_output);
        }
    }
    
    return "";
}

} // namespace utils

