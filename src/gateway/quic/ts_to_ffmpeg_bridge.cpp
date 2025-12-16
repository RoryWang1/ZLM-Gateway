#include "gateway/quic/ts_to_ffmpeg_bridge.hpp"
#include "process/process_manager.hpp"
#include "utils/logger.hpp"
#include "utils/ffmpeg_params.hpp"
#include "utils/zlm_url_builder.hpp"
#include "config/config_loader.hpp"
#include <sys/stat.h>
#include <sys/types.h>
#include <fcntl.h>
#include <unistd.h>
#include <cstdlib>
#include <sstream>
#include <cstring>
#include <errno.h>
#include <ctime>
#include <signal.h>

namespace gateway {
namespace quic {

TSToFFmpegBridge::TSToFFmpegBridge(const std::string& output_rtmp_url,
                                  std::shared_ptr<process::ProcessManager> process_manager)
    : output_rtmp_url_(output_rtmp_url), pipe_fd_(-1), ffmpeg_pid_(0),
      ffmpeg_running_(false), process_manager_(process_manager) {
    
    // 获取 FFmpeg 路径（从环境变量或默认路径）
    const char* ffmpeg_env = std::getenv("FFMPEG_PATH");
    if (ffmpeg_env) {
        ffmpeg_path_ = ffmpeg_env;
    } else {
        ffmpeg_path_ = "ffmpeg";  // 使用系统 PATH
    }
}

TSToFFmpegBridge::~TSToFFmpegBridge() {
    StopFFmpeg();
    if (pipe_fd_ >= 0) {
        close(pipe_fd_);
        pipe_fd_ = -1;
    }
    if (!pipe_path_.empty()) {
        unlink(pipe_path_.c_str());
    }
}

bool TSToFFmpegBridge::CreatePipe() {
    // 生成唯一的管道路径
    char tmp_path[256];
    snprintf(tmp_path, sizeof(tmp_path), "/tmp/quic_ts_pipe_%d_%lu", 
             getpid(), time(nullptr));
    pipe_path_ = tmp_path;
    
    // 创建命名管道（FIFO）
    if (mkfifo(pipe_path_.c_str(), 0666) < 0) {
        if (errno != EEXIST) {
            LOG_ERROR("[TSToFFmpegBridge] 创建命名管道失败: {} - {}", 
                     pipe_path_, strerror(errno));
            return false;
        }
    }
    
    // 注意：在 macOS 上，命名管道必须以阻塞模式打开
    // 或者先启动读取端（FFmpeg），然后再打开写入端
    // 这里先不打开，等 FFmpeg 启动后再打开
    // pipe_fd_ 将在 StartFFmpeg 中打开
    LOG_INFO("[TSToFFmpegBridge] 命名管道已创建，等待 FFmpeg 启动后打开: {}", pipe_path_);
    
    LOG_INFO("[TSToFFmpegBridge] 创建命名管道成功: {}", pipe_path_);
    return true;
}

bool TSToFFmpegBridge::WritePacket(const uint8_t* data, size_t len) {
    if (pipe_fd_ < 0) {
        return false;
    }
    
    std::lock_guard<std::mutex> lock(write_mutex_);
    
    ssize_t written = write(pipe_fd_, data, len);
    if (written < 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            // 非阻塞模式下管道满，这是正常的
            return true;
        } else {
            LOG_WARN("[TSToFFmpegBridge] 写入管道失败: {}", strerror(errno));
            return false;
        }
    }
    
    return static_cast<size_t>(written) == len;
}

bool TSToFFmpegBridge::StartFFmpeg(int video_width, int video_height, int bitrate_kbps) {
    if (ffmpeg_running_) {
        LOG_WARN("[TSToFFmpegBridge] FFmpeg 已在运行");
        return true;
    }
    
    if (pipe_path_.empty()) {
        LOG_ERROR("[TSToFFmpegBridge] 管道未创建");
        return false;
    }
    
    std::string command = BuildFFmpegCommand(video_width, video_height, bitrate_kbps);
    
    LOG_INFO("[TSToFFmpegBridge] 启动 FFmpeg: {}", command);
    
    // 使用进程管理器启动 FFmpeg（如果可用）
    if (process_manager_) {
        // TODO: 使用 ProcessManager 启动进程
        // 这里需要根据 ProcessManager 的 API 实现
        LOG_WARN("[TSToFFmpegBridge] ProcessManager 集成待实现");
    }
    
    // 直接使用 system() 启动（临时实现）
    // 注意：在后台启动 FFmpeg，这样它会打开管道的读取端
    std::string full_command = command + " > /dev/null 2>&1 &";
    int ret = system(full_command.c_str());
    
    if (ret != 0) {
        LOG_ERROR("[TSToFFmpegBridge] 启动 FFmpeg 失败");
        return false;
    }
    
    // 等待 FFmpeg 启动并打开管道（读取端）
    // 然后打开写入端
    for (int i = 0; i < 10; ++i) {
        usleep(100000);  // 100ms
        if (pipe_fd_ < 0) {
            pipe_fd_ = open(pipe_path_.c_str(), O_WRONLY | O_NONBLOCK);
            if (pipe_fd_ >= 0) {
                LOG_INFO("[TSToFFmpegBridge] 命名管道已打开: {}", pipe_path_);
                break;
            }
        }
    }
    
    if (pipe_fd_ < 0) {
        LOG_WARN("[TSToFFmpegBridge] 无法打开命名管道，FFmpeg 可能未正确启动");
        // 继续，可能 FFmpeg 会稍后打开
    }
    
    ffmpeg_running_ = true;
    // TODO: 获取实际的 PID
    ffmpeg_pid_ = 0;
    
    LOG_INFO("[TSToFFmpegBridge] FFmpeg 启动成功");
    return true;
}

bool TSToFFmpegBridge::StopFFmpeg() {
    if (!ffmpeg_running_) {
        return true;
    }
    
    if (ffmpeg_pid_ > 0) {
        kill(ffmpeg_pid_, SIGTERM);
        // 等待进程退出
        for (int i = 0; i < 50; ++i) {
            if (kill(ffmpeg_pid_, 0) != 0) {
                break;  // 进程已退出
            }
            usleep(100000);  // 100ms
        }
        
        // 如果还在运行，强制杀死
        if (kill(ffmpeg_pid_, 0) == 0) {
            kill(ffmpeg_pid_, SIGKILL);
        }
    }
    
    ffmpeg_running_ = false;
    ffmpeg_pid_ = 0;
    
    LOG_INFO("[TSToFFmpegBridge] FFmpeg 已停止");
    return true;
}

bool TSToFFmpegBridge::IsFFmpegRunning() const {
    if (!ffmpeg_running_ || ffmpeg_pid_ <= 0) {
        return false;
    }
    
    // 检查进程是否还在运行
    return kill(ffmpeg_pid_, 0) == 0;
}

std::string TSToFFmpegBridge::BuildFFmpegCommand(int video_width, int video_height, int bitrate_kbps) {
    std::ostringstream oss;
    
    oss << ffmpeg_path_
        << " -re"  // 实时读取
        << " -f mpegts"  // 输入格式为 MPEG-TS
        << " -i " << pipe_path_;  // 从命名管道读取
    
    // 转码参数
    if (bitrate_kbps > 0) {
        ::utils::FFmpegParams::AddVideoBitrateParams(oss, bitrate_kbps);
    }
    
    if (video_width > 0 && video_height > 0) {
        oss << " -s " << video_width << "x" << video_height;
    }
    
    oss << " -c:v libx264"
        << " -preset ultrafast"
        << " -tune zerolatency"
        << " -c:a aac"
        << " -b:a 128k"
        << " -f flv"  // 输出格式为 FLV（推流到 RTMP）
        << " \"" << output_rtmp_url_ << "\"";
    
    return oss.str();
}

} // namespace quic
} // namespace gateway

