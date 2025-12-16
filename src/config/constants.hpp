#pragma once

/**
 * @file constants.hpp
 * @brief 集中管理所有硬编码常量
 * @date 2025-12-13
 * 
 * Phase 1.1: 魔法数字消除
 * 将所有硬编码常量集中管理，便于配置和优化
 */

namespace config {
namespace constants {

// ============================================================================
// 时间相关常量 (毫秒/秒)
// ============================================================================
namespace time {
    // 进程相关
    constexpr int PROCESS_STABLE_WAIT_MS = 500;          // 进程启动稳定等待时间
    constexpr int FFMPEG_START_TIMEOUT_MS = 10000;       // FFmpeg启动超时
    constexpr int FFMPEG_STOP_WAIT_MS = 500;             // FFmpeg停止等待时间
    
    // ZLM流检查
    constexpr int ZLM_CHECK_INTERVAL_MS = 500;           // ZLM流状态检查间隔
    constexpr int ZLM_CHECK_TIMEOUT_MS = 10000;          // ZLM流就绪超时
    constexpr int ZLM_SYNC_INTERVAL_SEC = 30;            // ZLM状态同步间隔
    
    // GB28181协议超时
    // 注意：这些值是基于GB28181标准和实际测试得出的经验值
    constexpr int GB28181_INVITE_WAIT_MS = 2000;         // INVITE请求响应等待（设备通常需要1-3秒）
    constexpr int GB28181_BYE_WAIT_MS = 1500;            // BYE请求响应等待
    constexpr int GB28181_RETRY_INTERVAL_MS = 1000;      // 重试间隔
    constexpr int GB28181_RTP_WAIT_MS = 1000;            // RTP数据等待
    constexpr int GB28181_HEARTBEAT_INTERVAL_SEC = 60;   // 心跳间隔
    constexpr int GB28181_STATUS_CHECK_INTERVAL_SEC = 10; // 状态检查间隔
    
    // 设备发现超时
    constexpr int DEVICE_DISCOVERY_TIMEOUT_SEC = 5;      // 默认设备发现超时
    constexpr int ONVIF_RESPONSE_TIMEOUT_MS = 500;       // ONVIF WS-Discovery响应超时(标准值)
    constexpr int HTTP_DISCOVERY_TIMEOUT_SEC = 5;        // HTTP设备发现超时
    
    // 健康检查
    constexpr int HEALTH_CHECK_INTERVAL_SEC = 30;        // 健康检查间隔
    constexpr int STREAM_STATUS_CHECK_INTERVAL_SEC = 60; // 流状态检查间隔
    constexpr int PROCESS_MONITOR_INTERVAL_SEC = 1;      // 进程监控间隔
    
    // WebSocket
    constexpr int WEBSOCKET_RETRY_MS = 2000;             // WebSocket重连间隔
    constexpr int WEBSOCKET_PING_MS = 500;               // WebSocket ping间隔
    
    // HTTP Server
    constexpr int HTTP_SERVER_START_WAIT_MS = 100;       // HTTP服务器启动等待
    
    // Hook处理
    constexpr int HOOK_CHECK_INTERVAL_MS = 100;          // Hook检查间隔
    
    // 重试相关
    constexpr int SMART_STREAM_RETRY_INTERVAL_MS = 2000; // 智能流重试间隔
    constexpr int SMART_STREAM_FALLBACK_WAIT_MS = 3000;  // 智能流降级等待
    
    // SIP Server
    constexpr int SIP_POLL_INTERVAL_MS = 10;             // SIP轮询间隔
    constexpr int SIP_WAIT_MS = 100;                     // SIP等待时间
}

// ============================================================================
// 码率相关常量 (kbps/Mbps)
// ============================================================================
namespace bitrate {
    // 视频码率限制
    // 注意：这些值是基于1080p@30fps的经验值
    constexpr int MIN_BITRATE_KBPS = 2000;               // 最小码率 2 Mbps
    constexpr int MAX_BITRATE_KBPS = 12000;              // 最大码率 12 Mbps
    constexpr int DEFAULT_BITRATE_KBPS = 3500;           // 默认码率 3.5 Mbps
    
    // 音频码率
    constexpr int DEFAULT_AUDIO_BITRATE_KBPS = 128;      // 默认音频码率 128 kbps
    constexpr int AUDIO_SAMPLE_RATE_44100 = 44100;       // 采样率 44.1kHz
    constexpr int AUDIO_SAMPLE_RATE_48000 = 48000;       // 采样率 48kHz
    constexpr int AUDIO_CHANNELS_STEREO = 2;             // 立体声
    
    // 带宽分配
    constexpr int RESERVED_BANDWIDTH_MBPS = 5;           // 保留带宽 5 Mbps
    constexpr int BITRATE_UPDATE_INTERVAL_SEC = 10;      // 码率更新间隔
}

// ============================================================================
// 容量和限制常量
// ============================================================================
namespace capacity {
    constexpr int MAX_CONCURRENT_STREAMS = 1000;         // 最大并发流数
    constexpr int MAX_STREAM_LIST_SIZE = 1000;           // 流列表最大长度
    constexpr int MAX_RECOVER_THREADS = 10;              // 最大恢复线程数
    constexpr int DEFAULT_WORKER_THREADS = 4;            // 默认工作线程数
    constexpr int MAX_PROCESSES = 1000;                  // 最大进程数
}

// ============================================================================
// 编码相关常量
// ============================================================================
namespace encoding {
    // GOP设置
    constexpr int DEFAULT_GOP_SIZE = 30;                 // 默认GOP大小
    constexpr int WEBRTC_GOP_SIZE = 15;                  // WebRTC GOP大小（低延迟）
    
    // 帧率
    constexpr int DEFAULT_FPS = 30;                      // 默认帧率
    
    // 分辨率（常用）
    constexpr int RES_1080P_WIDTH = 1920;
    constexpr int RES_1080P_HEIGHT = 1080;
    constexpr int RES_720P_WIDTH = 1280;
    constexpr int RES_720P_HEIGHT = 720;
}

// ============================================================================
// 流验证相关常量
// ============================================================================
namespace validation {
    constexpr int MAX_CHECK_ATTEMPTS = 20;               // 最大检查次数
    constexpr int DETECT_TIMEOUT_DEFAULT_SEC = 20;       // 默认检测超时
    constexpr int DETECT_TIMEOUT_SHORT_SEC = 5;          // 短超时（HTTP-FLV）
}

// ============================================================================
// 清理策略常量
// ============================================================================
namespace cleanup {
    constexpr int ERROR_CLEANUP_SECONDS = 60;            // Error状态流清理时间
    constexpr int STOPPED_AFTER_ZLM_DELETE_SECONDS = 30; // ZLM删除后等待时间
}

} // namespace constants
} // namespace config
