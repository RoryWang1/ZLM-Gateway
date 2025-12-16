#include "config/config_loader.hpp"
#include <iostream>
#include <filesystem>

using json = nlohmann::json;

namespace config {

std::shared_ptr<Config> ConfigLoader::LoadFromFile(const std::string& config_path) {
    std::ifstream file(config_path);
    if (!file.is_open()) {
        // std::cerr << "无法打开配置文件: " << config_path << std::endl;
        // Note: std::cerr removed to prevent TTY suspension in background mode
        return nullptr;
    }

    std::stringstream buffer;
    buffer << file.rdbuf();
    file.close();

    return LoadFromString(buffer.str());
}

std::shared_ptr<Config> ConfigLoader::LoadFromString(const std::string& json_str) {
    try {
        json j = json::parse(json_str);
        auto config = std::make_shared<Config>();
        SetDefaults(*config);
        ParseJSON(j, *config);
        return config;
    } catch (const json::exception& e) {
        // std::cerr << "解析 JSON 配置失败: " << e.what() << std::endl;
        return nullptr;
    } catch (const std::exception& e) {
        // std::cerr << "加载配置失败: " << e.what() << std::endl;
        return nullptr;
    }
}

void ConfigLoader::ParseJSON(const json& j, Config& config) {
    // Gateway 配置
    if (j.contains("gateway")) {
        const auto& gw = j["gateway"];
        if (gw.contains("http_port")) config.gateway.http_port = gw["http_port"];
        if (gw.contains("ws_port")) config.gateway.ws_port = gw["ws_port"];
        if (gw.contains("log_level")) config.gateway.log_level = gw["log_level"];
        if (gw.contains("log_file")) config.gateway.log_file = gw["log_file"];
        if (gw.contains("total_bandwidth_mbps")) config.gateway.total_bandwidth_mbps = gw["total_bandwidth_mbps"];
        
        // 流验证配置
        if (gw.contains("stream_validation")) {
            const auto& sv = gw["stream_validation"];
            if (sv.contains("process_stable_wait_ms")) config.gateway.stream_validation.process_stable_wait_ms = sv["process_stable_wait_ms"];
            if (sv.contains("zlm_check_interval_ms")) config.gateway.stream_validation.zlm_check_interval_ms = sv["zlm_check_interval_ms"];
            if (sv.contains("zlm_check_timeout_ms")) config.gateway.stream_validation.zlm_check_timeout_ms = sv["zlm_check_timeout_ms"];
            if (sv.contains("max_check_attempts")) config.gateway.stream_validation.max_check_attempts = sv["max_check_attempts"];
        }
        
        // 直接代理配置
        if (gw.contains("direct_proxy")) {
            const auto& dp = gw["direct_proxy"];
            if (dp.contains("max_retries")) config.gateway.direct_proxy.max_retries = dp["max_retries"];
            if (dp.contains("retry_interval_ms")) config.gateway.direct_proxy.retry_interval_ms = dp["retry_interval_ms"];
        }
        
        // 流信息检测配置
        if (gw.contains("stream_detection")) {
            const auto& sd = gw["stream_detection"];
            if (sd.contains("timeout_seconds")) config.gateway.stream_detection.timeout_seconds = sd["timeout_seconds"];
        }
    }

    // ZLMediaKit 配置
    if (j.contains("zlmediakit")) {
        const auto& zlm = j["zlmediakit"];
        if (zlm.contains("api_url")) config.zlmediakit.api_url = zlm["api_url"];
        if (zlm.contains("secret")) config.zlmediakit.secret = zlm["secret"];
        if (zlm.contains("rtmp_port")) config.zlmediakit.rtmp_port = zlm["rtmp_port"];
        if (zlm.contains("rtsp_port")) config.zlmediakit.rtsp_port = zlm["rtsp_port"];
        if (zlm.contains("http_port")) config.zlmediakit.http_port = zlm["http_port"];
    }

        // RTSP 配置
        if (j.contains("rtsp")) {
            const auto& rtsp = j["rtsp"];
            if (rtsp.contains("enabled")) config.rtsp.enabled = rtsp["enabled"];
            if (rtsp.contains("use_gstreamer")) config.rtsp.use_gstreamer = rtsp["use_gstreamer"];
            if (rtsp.contains("ffmpeg_path")) config.rtsp.ffmpeg_path = rtsp["ffmpeg_path"];
            if (rtsp.contains("ffprobe_path")) config.rtsp.ffprobe_path = rtsp["ffprobe_path"];
        }

        // 进程管理配置
        if (j.contains("process")) {
            const auto& process = j["process"];
            if (process.contains("max_restarts")) config.process.max_restarts = process["max_restarts"];
            if (process.contains("monitor_interval")) config.process.monitor_interval = process["monitor_interval"];
            if (process.contains("max_processes")) config.process.max_processes = process["max_processes"];
            if (process.contains("default_cpu_limit")) config.process.default_cpu_limit = process["default_cpu_limit"];
            if (process.contains("default_memory_limit")) config.process.default_memory_limit = process["default_memory_limit"];
        }

    // RTMP 配置
    if (j.contains("rtmp")) {
        const auto& rtmp = j["rtmp"];
        if (rtmp.contains("enabled")) config.rtmp.enabled = rtmp["enabled"];
    }

    // ONVIF 配置
    if (j.contains("onvif")) {
        const auto& onvif = j["onvif"];
        if (onvif.contains("enabled")) config.onvif.enabled = onvif["enabled"];
        if (onvif.contains("discovery_timeout")) config.onvif.discovery_timeout = onvif["discovery_timeout"];
    }

    // ISAPI 配置
    if (j.contains("isapi")) {
        const auto& isapi = j["isapi"];
        if (isapi.contains("enabled")) config.isapi.enabled = isapi["enabled"];
    }

    // 大华配置
    if (j.contains("dahua")) {
        const auto& dahua = j["dahua"];
        if (dahua.contains("enabled")) config.dahua.enabled = dahua["enabled"];
    }

    // PSIA 配置
    if (j.contains("psia")) {
        const auto& psia = j["psia"];
        if (psia.contains("enabled")) config.psia.enabled = psia["enabled"];
    }

    // GB28181 配置
    if (j.contains("gb28181")) {
        const auto& gb28181 = j["gb28181"];
        if (gb28181.contains("enabled")) config.gb28181.enabled = gb28181["enabled"];
        if (gb28181.contains("server_id")) config.gb28181.server_id = gb28181["server_id"];
        if (gb28181.contains("domain")) config.gb28181.domain = gb28181["domain"];
        if (gb28181.contains("local_ip")) config.gb28181.local_ip = gb28181["local_ip"];
        if (gb28181.contains("local_port")) config.gb28181.local_port = gb28181["local_port"];
        if (gb28181.contains("heartbeat_timeout_seconds")) config.gb28181.heartbeat_timeout_seconds = gb28181["heartbeat_timeout_seconds"];
        if (gb28181.contains("auto_remove_offline")) config.gb28181.auto_remove_offline = gb28181["auto_remove_offline"];
        if (gb28181.contains("default_tcp_mode")) config.gb28181.default_tcp_mode = gb28181["default_tcp_mode"];
        if (gb28181.contains("default_enable_rtcp")) config.gb28181.default_enable_rtcp = gb28181["default_enable_rtcp"];
        if (gb28181.contains("rtp_port_range")) config.gb28181.rtp_port_range = gb28181["rtp_port_range"];
        if (gb28181.contains("stream_start_timeout_seconds")) config.gb28181.stream_start_timeout_seconds = gb28181["stream_start_timeout_seconds"];
    }

    // QUIC 配置
    if (j.contains("quic")) {
        const auto& quic = j["quic"];
        if (quic.contains("enabled")) config.quic.enabled = quic["enabled"];
        if (quic.contains("bind_address")) config.quic.bind_address = quic["bind_address"];
        if (quic.contains("port")) config.quic.port = quic["port"];
        if (quic.contains("max_connections")) config.quic.max_connections = quic["max_connections"];
        
        // FEC 配置
        if (quic.contains("fec")) {
            const auto& fec = quic["fec"];
            if (fec.contains("algorithm")) config.quic.fec.algorithm = fec["algorithm"];
            if (fec.contains("params")) {
                const auto& params = fec["params"];
                if (params.contains("L")) config.quic.fec.L = params["L"];
                if (params.contains("D")) config.quic.fec.D = params["D"];
            }
        }
        
        // TLS 配置
        if (quic.contains("tls")) {
            const auto& tls = quic["tls"];
            if (tls.contains("cert_path")) config.quic.tls.cert_path = tls["cert_path"];
            if (tls.contains("key_path")) config.quic.tls.key_path = tls["key_path"];
        }
    }

    // DASH 配置
    if (j.contains("dash")) {
        const auto& dash = j["dash"];
        if (dash.contains("enabled")) config.dash.enabled = dash["enabled"];
    }

    // NDI 配置
    if (j.contains("ndi")) {
        const auto& ndi = j["ndi"];
        if (ndi.contains("enabled")) config.ndi.enabled = ndi["enabled"];
    }

    // Local Camera 配置
    if (j.contains("local_camera")) {
        const auto& local_camera = j["local_camera"];
        if (local_camera.contains("enabled")) config.local_camera.enabled = local_camera["enabled"];
        if (local_camera.contains("default_resolution")) config.local_camera.default_resolution = local_camera["default_resolution"];
        if (local_camera.contains("default_fps")) config.local_camera.default_fps = local_camera["default_fps"];
        if (local_camera.contains("default_bitrate")) config.local_camera.default_bitrate = local_camera["default_bitrate"];
        if (local_camera.contains("encoding_preset")) config.local_camera.encoding_preset = local_camera["encoding_preset"];
        if (local_camera.contains("encoding_tune")) config.local_camera.encoding_tune = local_camera["encoding_tune"];
        if (local_camera.contains("device_cache_ttl_seconds")) config.local_camera.device_cache_ttl_seconds = local_camera["device_cache_ttl_seconds"];
        
        // 流启动配置
        if (local_camera.contains("stream_start")) {
            const auto& stream_start = local_camera["stream_start"];
            if (stream_start.contains("process_start_wait_ms")) config.local_camera.stream_start.process_start_wait_ms = stream_start["process_start_wait_ms"];
            if (stream_start.contains("process_stable_wait_ms")) config.local_camera.stream_start.process_stable_wait_ms = stream_start["process_stable_wait_ms"];
            if (stream_start.contains("zlm_check_interval_ms")) config.local_camera.stream_start.zlm_check_interval_ms = stream_start["zlm_check_interval_ms"];
            if (stream_start.contains("zlm_check_timeout_ms")) config.local_camera.stream_start.zlm_check_timeout_ms = stream_start["zlm_check_timeout_ms"];
            if (stream_start.contains("max_check_attempts")) config.local_camera.stream_start.max_check_attempts = stream_start["max_check_attempts"];
        }
        
        // 流健康监控配置
        if (local_camera.contains("health_monitor")) {
            const auto& health_monitor = local_camera["health_monitor"];
            if (health_monitor.contains("check_interval_seconds")) config.local_camera.health_monitor.check_interval_seconds = health_monitor["check_interval_seconds"];
            if (health_monitor.contains("auto_recover")) config.local_camera.health_monitor.auto_recover = health_monitor["auto_recover"];
            if (health_monitor.contains("max_recover_attempts")) config.local_camera.health_monitor.max_recover_attempts = health_monitor["max_recover_attempts"];
            if (health_monitor.contains("recover_cooldown_seconds")) config.local_camera.health_monitor.recover_cooldown_seconds = health_monitor["recover_cooldown_seconds"];
        }
        
        // WebRTC 配置
        if (local_camera.contains("webrtc")) {
            const auto& webrtc = local_camera["webrtc"];
            if (webrtc.contains("resolution")) config.local_camera.webrtc.resolution = webrtc["resolution"];
            if (webrtc.contains("fps")) config.local_camera.webrtc.fps = webrtc["fps"];
            if (webrtc.contains("bitrate")) config.local_camera.webrtc.bitrate = webrtc["bitrate"];
            if (webrtc.contains("preset")) config.local_camera.webrtc.preset = webrtc["preset"];
            if (webrtc.contains("gop_size")) config.local_camera.webrtc.gop_size = webrtc["gop_size"];
            if (webrtc.contains("threads")) config.local_camera.webrtc.threads = webrtc["threads"];
        }
        
        // FLV/HLS 配置
        if (local_camera.contains("flv_hls")) {
            const auto& flv_hls = local_camera["flv_hls"];
            if (flv_hls.contains("resolution")) config.local_camera.flv_hls.resolution = flv_hls["resolution"];
            if (flv_hls.contains("fps")) config.local_camera.flv_hls.fps = flv_hls["fps"];
            if (flv_hls.contains("bitrate")) config.local_camera.flv_hls.bitrate = flv_hls["bitrate"];
            if (flv_hls.contains("preset")) config.local_camera.flv_hls.preset = flv_hls["preset"];
            if (flv_hls.contains("gop_size")) config.local_camera.flv_hls.gop_size = flv_hls["gop_size"];
        }
    }
}

void ConfigLoader::SetDefaults(Config& /* config */) {
    // 默认值已在结构体定义中设置
    // 这里可以添加额外的默认值设置逻辑
    // 参数暂时未使用，使用注释参数名避免警告
}

} // namespace config

