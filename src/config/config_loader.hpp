#ifndef CONFIG_CONFIG_LOADER_HPP
#define CONFIG_CONFIG_LOADER_HPP

#include <fstream>
#include <memory>
#include <nlohmann/json.hpp>
#include <sstream>
#include <string>

namespace config {

/**
 * @brief 配置结构体
 */
struct Config {
  // Gateway 配置
  struct GatewayConfig {
    int http_port = 8080;
    int ws_port =
        8092; // WebSocket 端口，默认 8092 避免与 ffmpeg 或其他服务冲突
    std::string log_level = "info";
    std::string log_file = "logs/gateway.log";
    int total_bandwidth_mbps =
        0; // 总带宽（Mbps），0表示未配置，将使用系统检测或默认值

    // 流验证配置（所有Gateway通用）
    struct StreamValidationConfig {
      int process_stable_wait_ms = 500; // 进程稳定等待时间（毫秒）
      int zlm_check_interval_ms = 500;  // ZLM检查间隔（毫秒）
      int zlm_check_timeout_ms = 10000; // ZLM检查超时（毫秒）
      int max_check_attempts = 20;      // 最大检查次数
    } stream_validation;

    // 直接代理配置
    struct DirectProxyConfig {
      int max_retries = 5;          // 最大重试次数
      int retry_interval_ms = 2000; // 重试间隔（毫秒）
    } direct_proxy;

    // 流信息检测配置
    struct StreamDetectionConfig {
      int timeout_seconds = 10; // 检测超时（秒）
    } stream_detection;
  } gateway;

  // ZLMediaKit 配置
  struct ZLMediaKitConfig {
    std::string api_url = "http://localhost:80";
    std::string secret = "";
    int rtmp_port = 1935;
    int rtsp_port = 554;
    int http_port = 80;
  } zlmediakit;

  // RTSP Gateway 配置
  struct RTSPConfig {
    bool enabled = true;

    std::string ffmpeg_path = "";
    std::string ffprobe_path = "";
  } rtsp;

  // 进程管理配置
  struct ProcessConfig {
    int max_restarts = 3;     // 最大重启次数
    int monitor_interval = 5; // 监控间隔（秒）
    int max_processes = 0;    // 最大进程数（0 表示无限制）
    int default_cpu_limit = 0; // 默认 CPU 限制（百分比，0 表示无限制）
    int64_t default_memory_limit = 0; // 默认内存限制（字节，0 表示无限制）
  } process;

  // RTMP Gateway 配置
  struct RTMPConfig {
    bool enabled = true;
  } rtmp;

  // ONVIF Gateway 配置
  struct ONVIFConfig {
    bool enabled = true;
    int discovery_timeout = 5;
  } onvif;

  // ISAPI Gateway 配置
  struct ISAPIConfig {
    bool enabled = true;
  } isapi;

  // 大华 Gateway 配置
  struct DahuaConfig {
    bool enabled = true;
  } dahua;

  // PSIA Gateway 配置
  struct PSIAConfig {
    bool enabled = true;
  } psia;

  // GB28181 Gateway 配置
  struct GB28181Config {
    bool enabled = true;
    std::string server_id = "34020000002000000001"; // 服务器ID（20位国标ID）
    std::string domain = "3402000000";              // 域（10位数字）
    std::string local_ip = "0.0.0.0"; // SIP服务器绑定IP（0.0.0.0表示所有接口）
    int local_port = 5060;               // SIP服务器端口
    int heartbeat_timeout_seconds = 300; // 心跳超时时间（秒）
    bool auto_remove_offline = false;    // 是否自动删除离线设备
    int default_tcp_mode = 0; // 默认TCP模式（0=UDP, 1=TCP被动, 2=TCP主动）
    bool default_enable_rtcp = false;           // 默认是否启用RTCP
    std::string rtp_port_range = "30000-35000"; // RTP端口范围
    int stream_start_timeout_seconds =
        60; // 流启动超时时间（秒），从发送INVITE到收到200 OK的超时
  } gb28181;

  // QUIC Gateway 配置
  struct QUICConfig {
    bool enabled = true;
    std::string bind_address = "0.0.0.0";
    int port = 4433;
    int max_connections = 100;

    // FEC 配置
    struct FECConfig {
      std::string algorithm =
          "smpte_2022_1"; // smpte_2022_1, raptorq, reed_solomon
      int L = 10;         // 列数（SMPTE 2022-1）
      int D = 10;         // 行数（SMPTE 2022-1）
    } fec;

    // TLS 配置（QUIC 需要 TLS）
    struct TLSConfig {
      std::string cert_path;
      std::string key_path;
    } tls;
  } quic;

  // DASH Gateway 配置
  struct DASHConfig {
    bool enabled = true;
  } dash;

  // NDI Gateway 配置
  struct NDIConfig {
    bool enabled = true;
  } ndi;

  // Local Camera Gateway 配置
  struct LocalCameraConfig {
    bool enabled = true;
    std::string default_resolution = "1280x720";
    int default_fps = 30;
    std::string default_bitrate = "2000k";
    std::string encoding_preset = "veryfast";
    std::string encoding_tune = "zerolatency";

    // WebRTC 专用配置
    struct WebRTCConfig {
      std::string resolution = "1280x720";
      int fps = 30;
      std::string bitrate = "1200k";
      std::string preset = "ultrafast";
      int gop_size = 10;
      int threads = 1;
    } webrtc;

    // HTTP-FLV/HLS 专用配置
    struct FLVHLSConfig {
      std::string resolution = "1280x720";
      int fps = 30;
      std::string bitrate = "1800k";
      std::string preset = "veryfast";
      int gop_size = 25;
    } flv_hls;

    // 设备发现配置
    int device_cache_ttl_seconds = 30; // 设备缓存过期时间（秒）

    // 流启动配置
    struct StreamStartConfig {
      int process_start_wait_ms = 500; // 进程启动后等待时间（毫秒）
      int process_stable_wait_ms = 2000; // 进程稳定等待时间（毫秒）
      int zlm_check_interval_ms = 3000;  // ZLM 流检查间隔（毫秒）
      int zlm_check_timeout_ms = 10000;  // ZLM 流检查超时（毫秒）
      int max_check_attempts = 2;        // 最大检查次数
    } stream_start;

    // 流健康监控配置
    struct HealthMonitorConfig {
      int check_interval_seconds = 10;   // 健康检查间隔（秒）
      bool auto_recover = false;         // 是否自动恢复失败的流
      int max_recover_attempts = 3;      // 最大恢复尝试次数
      int recover_cooldown_seconds = 30; // 恢复冷却时间（秒）
    } health_monitor;
  } local_camera;
};

/**
 * @brief 配置加载器
 */
class ConfigLoader {
public:
  /**
   * @brief 从文件加载配置
   * @param config_path 配置文件路径
   * @return 配置对象，失败返回 nullptr
   */
  static std::shared_ptr<Config> LoadFromFile(const std::string &config_path);

  /**
   * @brief 从 JSON 字符串加载配置
   * @param json_str JSON 字符串
   * @return 配置对象，失败返回 nullptr
   */
  static std::shared_ptr<Config> LoadFromString(const std::string &json_str);

private:
  /**
   * @brief 解析 JSON 对象到配置结构
   */
  static void ParseJSON(const nlohmann::json &j, Config &config);

  /**
   * @brief 设置默认值
   */
  static void SetDefaults(Config &config);
};

} // namespace config

#endif // CONFIG_CONFIG_LOADER_HPP
