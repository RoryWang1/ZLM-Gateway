#include <iostream>
#include <signal.h>
#include <unistd.h>
#include <fcntl.h>
#include <fstream>
#include <sstream>
#include <algorithm>
#include <cctype>
#include "config/config_loader.hpp"
#include "utils/logger.hpp"
#include "streaming/zlmediakit/zlm_client.hpp"
#include "streaming/stream_manager.hpp"
#include "gateway/utils/gateway_factory.hpp"
#include "process/process_manager.hpp"
#include "api/http_server.hpp"
#include "api/websocket_server.hpp"
#include "utils/bitrate_allocator.hpp"

// Gateway includes are handled via factory
#include "gateway/utils/gateway_factory.hpp"

// 全局变量，用于信号处理
static std::shared_ptr<api::HttpServer> g_http_server = nullptr;
static std::shared_ptr<api::WebSocketServer> g_websocket_server = nullptr;
static volatile bool g_running = true;

// 信号处理函数
void SignalHandler(int signal) {
    if (signal == SIGINT || signal == SIGTERM) {
        LOG_INFO("收到退出信号，正在关闭服务器...");
        g_running = false;
        if (g_http_server) {
            g_http_server->Stop();
        }
        if (g_websocket_server) {
            g_websocket_server->Stop();
        }
    }
}
/**
 * @brief 检测系统网络带宽（Mbps）
 * @return 检测到的带宽（Mbps），如果检测失败返回0
 */
static int DetectSystemBandwidth() {
#ifdef __APPLE__
    // macOS: 检测主要网络接口并估算带宽
    // 方法1: 查找活跃的网络接口（排除lo0）
    FILE* pipe = popen("route get default 2>/dev/null | grep interface | awk '{print $2}'", "r");
    if (pipe) {
        char buffer[128];
        if (fgets(buffer, sizeof(buffer), pipe) != nullptr) {
            std::string interface_name = buffer;
            interface_name.erase(interface_name.find_last_not_of(" \t\n") + 1);
            
            if (!interface_name.empty() && interface_name != "lo0") {
                // 根据接口名称判断类型
                std::string lower_name = interface_name;
                std::transform(lower_name.begin(), lower_name.end(), lower_name.begin(), ::tolower);
                
                // 检查是否是Wi-Fi接口（通常是en0或en1）
                if (interface_name == "en0" || interface_name == "en1") {
                    // 尝试使用networksetup获取Wi-Fi信息
                    std::string cmd = "networksetup -getairportnetwork " + interface_name + " 2>/dev/null";
                    FILE* wifi_pipe = popen(cmd.c_str(), "r");
                    if (wifi_pipe) {
                        pclose(wifi_pipe);
                        // 是Wi-Fi接口，保守估计300Mbps
                        pclose(pipe);
                        return 300;
                    }
                }
                
                // 以太网接口（en2-en9等），保守估计1000Mbps
                if (interface_name.find("en") == 0) {
                    pclose(pipe);
                    return 1000;
                }
            }
        }
        pclose(pipe);
    }
    
    // 方法2: 使用networksetup查找主要接口
    pipe = popen("networksetup -listallhardwareports 2>/dev/null | grep -A 1 'Hardware Port' | grep -E 'Ethernet|Wi-Fi' | head -1", "r");
    if (pipe) {
        char buffer[128];
        if (fgets(buffer, sizeof(buffer), pipe) != nullptr) {
            std::string line(buffer);
            std::transform(line.begin(), line.end(), line.begin(), ::tolower);
            
            if (line.find("wi-fi") != std::string::npos || line.find("wifi") != std::string::npos) {
                pclose(pipe);
                return 300;  // Wi-Fi: 保守估计300Mbps
            } else if (line.find("ethernet") != std::string::npos) {
                pclose(pipe);
                return 1000;  // 以太网: 保守估计1000Mbps
            }
        }
        pclose(pipe);
    }
    
    // 方法3: 查找第一个非lo0的活跃接口
    pipe = popen("ifconfig | grep -E '^[a-z]' | grep -v '^lo0:' | head -1 | cut -d: -f1", "r");
    if (pipe) {
        char buffer[128];
        if (fgets(buffer, sizeof(buffer), pipe) != nullptr) {
            std::string interface_name = buffer;
            interface_name.erase(interface_name.find_last_not_of(" \t\n") + 1);
            
            if (!interface_name.empty() && interface_name.find("en") == 0) {
                pclose(pipe);
                // 根据接口编号判断：en0/en1通常是Wi-Fi，其他是以太网
                if (interface_name == "en0" || interface_name == "en1") {
                    return 300;  // Wi-Fi
                } else {
                    return 1000;  // 以太网
                }
            }
        }
        pclose(pipe);
    }
    
    // 检测失败
    return 0;
    
#elif __linux__
    // Linux: 使用 /sys/class/net/ 检测网络接口速度
    // 查找第一个活跃的网络接口
    FILE* pipe = popen("ip -o link show | grep -v 'lo:' | head -1 | cut -d: -f2 | awk '{print $1}'", "r");
    if (!pipe) {
        return 0;
    }
    
    char buffer[128];
    std::string interface_name;
    if (fgets(buffer, sizeof(buffer), pipe) != nullptr) {
        interface_name = buffer;
        interface_name.erase(interface_name.find_last_not_of(" \t\n") + 1);
    }
    pclose(pipe);
    
    if (!interface_name.empty()) {
        // 读取接口速度（单位：Mbps）
        std::string speed_path = "/sys/class/net/" + interface_name + "/speed";
        std::ifstream speed_file(speed_path);
        if (speed_file.is_open()) {
            int speed_mbps = 0;
            speed_file >> speed_mbps;
            speed_file.close();
            if (speed_mbps > 0) {
                return speed_mbps;
            }
        }
        
        // 如果无法读取speed文件，尝试使用ethtool
        std::string cmd = "ethtool " + interface_name + " 2>/dev/null | grep -i 'Speed:' | awk '{print $2}' | sed 's/Mb\\/s//'";
        pipe = popen(cmd.c_str(), "r");
        if (pipe) {
            if (fgets(buffer, sizeof(buffer), pipe) != nullptr) {
                try {
                    int speed = std::stoi(buffer);
                    if (speed > 0) {
                        pclose(pipe);
                        return speed;
                    }
                } catch (...) {
                    // 解析失败
                }
            }
            pclose(pipe);
        }
    }
    
    return 0;
#else
    // 其他平台：无法检测
    return 0;
#endif
}



int main(int argc, char* argv[]) {
    // 解析命令行参数
    std::string config_path = "configs/config.json";
    if (argc > 1) {
        config_path = argv[1];
    }

    // 加载配置
    auto config = config::ConfigLoader::LoadFromFile(config_path);
    if (!config) {
        return 1;
    }

    // 初始化日志系统
    utils::Logger::Initialize(
        config->gateway.log_level,
        config->gateway.log_file
    );

    LOG_INFO("==========================================");
    LOG_INFO("ZLM Gateway 启动");
    LOG_INFO("==========================================");
    LOG_INFO("配置文件: {}", config_path);
    LOG_INFO("日志级别: {}", config->gateway.log_level);
    LOG_INFO("日志文件: {}", config->gateway.log_file);

    // 创建 ZLMediaKit 客户端
    auto zlm_client = std::make_shared<streaming::ZLMClient>(
        config->zlmediakit.api_url,
        config->zlmediakit.secret
    );

    // 检查 ZLMediaKit 是否在线
    LOG_INFO("检查 ZLMediaKit 连接: {}", config->zlmediakit.api_url);
    if (!zlm_client->IsOnline()) {
        LOG_WARN("ZLMediaKit 服务器未响应，请确保服务器正在运行");
    } else {
        LOG_INFO("ZLMediaKit 服务器连接成功");
    }

    // 创建 WebSocket Server（从配置文件读取端口）
    int ws_port = config->gateway.ws_port;
    g_websocket_server = std::make_shared<api::WebSocketServer>(ws_port);
    if (!g_websocket_server->Start()) {
        // WebSocket 启动失败（通常是端口占用）不应阻止主程序运行，只记录警告
        ::utils::Logger::Get()->warn("WebSocket 服务器启动失败，端口: {} (可能已被占用，不影响主程序运行)", ws_port);
    }

    // 创建 Process Manager（统一的 FFmpeg 进程管理）
    auto process_manager = std::make_shared<process::ProcessManager>(
        config->process.max_restarts,
        config->process.monitor_interval,
        config->process.max_processes
    );

    // 创建 Stream Manager (传入 WebSocket Server)
    auto stream_manager = std::make_shared<streaming::StreamManager>(zlm_client, g_websocket_server);
    
    // 启动状态同步线程（每 10 秒同步一次 ZLMediaKit 状态）
    stream_manager->StartStatusSync(10);
    LOG_INFO("Stream Manager 已启动，状态同步间隔: 10 秒");

    // 创建智能码率分配器（需要在创建Gateway之前创建）
    utils::BitrateAllocatorConfig bitrate_config;
    
    // 从配置文件读取总带宽，如果未配置则尝试检测或使用默认值
    int total_bandwidth_mbps = config->gateway.total_bandwidth_mbps;
    if (total_bandwidth_mbps <= 0) {
        // 尝试从系统检测网络接口速度
        total_bandwidth_mbps = DetectSystemBandwidth();
        if (total_bandwidth_mbps <= 0) {
            total_bandwidth_mbps = 100;  // 如果检测失败，使用默认值100Mbps
            LOG_WARN("无法检测系统带宽，使用默认值: {} Mbps", total_bandwidth_mbps);
        } else {
            LOG_INFO("检测到系统带宽: {} Mbps", total_bandwidth_mbps);
        }
    } else {
        LOG_INFO("从配置文件读取总带宽: {} Mbps", total_bandwidth_mbps);
    }
    bitrate_config.total_bandwidth_mbps = total_bandwidth_mbps;
    // 提高单路流的基础码率档位：不再过度省带宽
    // - 最小码率：2000 kbps（保证单路画质不要太差）
    // - 默认码率：3500 kbps（适合 720p/1080p 中高质量）
    // - 最大码率：12000 kbps（给高分辨率/高帧率更多空间）
    bitrate_config.min_bitrate_kbps = 2000;
    bitrate_config.max_bitrate_kbps = 12000;
    bitrate_config.default_bitrate_kbps = 3500;
    // 适当减少预留带宽，让业务流能用到更多可用带宽
    bitrate_config.reserved_bandwidth_mbps = 5;
    bitrate_config.enable_adaptive = true;
    bitrate_config.update_interval_sec = 10;
    
    auto bitrate_allocator = std::make_shared<utils::BitrateAllocator>(bitrate_config, zlm_client);
    LOG_INFO("智能码率分配器已创建，总带宽: {} Mbps", bitrate_config.total_bandwidth_mbps);

    // 使用工厂类创建所有Gateway实例
    auto gateways = gateway::utils::GatewayFactory::CreateAllGateways(
        config, zlm_client, process_manager, stream_manager, bitrate_allocator, g_websocket_server);
    
    // 创建 HTTP API 服务器
    LOG_INFO("正在创建 HTTP API 服务器...");
    // Phase 6.2: Pass generic gateway instances
    g_http_server = std::make_shared<api::HttpServer>(
        config, zlm_client, stream_manager, process_manager, gateways);
    LOG_INFO("HTTP API 服务器创建完成");

    // 注册信号处理
    signal(SIGINT, SignalHandler);
    signal(SIGTERM, SignalHandler);

    // 启动 HTTP 服务器
    LOG_INFO("正在启动 HTTP API 服务器...");
    if (!g_http_server->Start()) {
        LOG_ERROR("启动 HTTP 服务器失败");
        return 1;
    }
    LOG_INFO("HTTP API 服务器启动成功");

    LOG_INFO("==========================================");
    LOG_INFO("ZLM Gateway 运行中...");
    LOG_INFO("HTTP API: http://0.0.0.0:{}", config->gateway.http_port);
    LOG_INFO("WebSocket: ws://0.0.0.0:{}", ws_port);
    LOG_INFO("按 Ctrl+C 退出");
    LOG_INFO("==========================================");

    // 主循环
    while (g_running && g_http_server->IsRunning()) {
        sleep(1);
    }

    LOG_INFO("ZLM Gateway 已退出");
    
    // 显式释放全局对象，确保析构顺序（在 Logger 销毁前释放）
    // 防止静态变量析构顺序导致的崩溃（Static Initialization Order Fiasco）
    g_http_server.reset();
    g_websocket_server.reset();
    
    return 0;
}

