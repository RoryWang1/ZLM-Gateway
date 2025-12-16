#ifndef GATEWAY_QUIC_TS_TO_FFMPEG_BRIDGE_HPP
#define GATEWAY_QUIC_TS_TO_FFMPEG_BRIDGE_HPP

#include <string>
#include <memory>
#include <functional>
#include <thread>
#include <atomic>
#include <mutex>
#include <cstdint>
#include <cstddef>

namespace process {
class ProcessManager;
}

namespace gateway {
namespace quic {

/**
 * @brief TS 到 FFmpeg 的桥接
 * 
 * 通过命名管道将 TS 流传递给 FFmpeg 进程
 */
class TSToFFmpegBridge {
public:
    /**
     * @brief 构造函数
     * @param output_rtmp_url 输出 RTMP URL（推流到 ZLM）
     * @param process_manager 进程管理器（可选）
     */
    TSToFFmpegBridge(const std::string& output_rtmp_url,
                    std::shared_ptr<process::ProcessManager> process_manager = nullptr);
    
    /**
     * @brief 析构函数
     */
    ~TSToFFmpegBridge();
    
    /**
     * @brief 创建命名管道
     * @return 是否成功
     */
    bool CreatePipe();
    
    /**
     * @brief 写入 TS 包到管道
     * @param data TS 包数据
     * @param len 长度
     * @return 是否成功
     */
    bool WritePacket(const uint8_t* data, size_t len);
    
    /**
     * @brief 启动 FFmpeg 进程
     * @param video_width 视频宽度（0 表示自动检测）
     * @param video_height 视频高度（0 表示自动检测）
     * @param bitrate_kbps 码率（kbps，0 表示使用默认值）
     * @return 是否成功
     */
    bool StartFFmpeg(int video_width = 0, int video_height = 0, int bitrate_kbps = 0);
    
    /**
     * @brief 停止 FFmpeg 进程
     * @return 是否成功
     */
    bool StopFFmpeg();
    
    /**
     * @brief 检查 FFmpeg 是否运行中
     * @return 是否运行中
     */
    bool IsFFmpegRunning() const;
    
    /**
     * @brief 获取 FFmpeg 进程 PID
     * @return PID，0 表示未运行
     */
    pid_t GetFFmpegPID() const { return ffmpeg_pid_; }
    
    /**
     * @brief 获取管道路径
     * @return 管道路径
     */
    std::string GetPipePath() const { return pipe_path_; }

private:
    /**
     * @brief 构建 FFmpeg 命令
     * @param video_width 视频宽度
     * @param video_height 视频高度
     * @param bitrate_kbps 码率
     * @return FFmpeg 命令字符串
     */
    std::string BuildFFmpegCommand(int video_width, int video_height, int bitrate_kbps);
    
    std::string output_rtmp_url_;
    std::string pipe_path_;
    int pipe_fd_;
    pid_t ffmpeg_pid_;
    std::atomic<bool> ffmpeg_running_;
    std::shared_ptr<process::ProcessManager> process_manager_;
    std::mutex write_mutex_;
    
    // FFmpeg 路径
    std::string ffmpeg_path_;
};

} // namespace quic
} // namespace gateway

#endif // GATEWAY_QUIC_TS_TO_FFMPEG_BRIDGE_HPP

