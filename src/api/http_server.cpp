#include "api/http_server.hpp"
#include "api/utils/route_helper.hpp"
#include "api/handlers/hook_handler.hpp"
#include "gateway/rtsp/rtsp_gateway.hpp"
#include "gateway/rtmp/rtmp_gateway.hpp"
#include "gateway/httpflv/httpflv_gateway.hpp"
#include "gateway/dash/dash_gateway.hpp"
#include "gateway/hls/hls_gateway.hpp"
#include "gateway/quic/quic_gateway.hpp"
#include "gateway/onvif/onvif_gateway.hpp"
#include "gateway/isapi/isapi_gateway.hpp"
#include "gateway/dahua/dahua_gateway.hpp"
#include "gateway/psia/psia_gateway.hpp"
#include "gateway/local_camera/local_camera_gateway.hpp"
#include "gateway/gb28181/gb28181_gateway.hpp"
#include "api/utils/response_helper.hpp"
#include "process/process_manager.hpp"
#include "config/constants.hpp"
#include "utils/logger.hpp"
#include "utils/system_utils.hpp"
#include <nlohmann/json.hpp>
#include <nlohmann/json.hpp>
#include <thread>
#include <chrono>
#include <mutex>
#include <atomic>

using namespace config::constants;

using json = nlohmann::json;

namespace api {


HttpServer::HttpServer(std::shared_ptr<config::Config> config,
                       std::shared_ptr<streaming::ZLMClient> zlm_client,
                       std::shared_ptr<streaming::StreamManager> stream_manager,
                       std::shared_ptr<process::ProcessManager> process_manager,
                       const gateway::utils::GatewayFactory::GatewayInstances& gateways)
    : config_(config), zlm_client_(zlm_client), stream_manager_(stream_manager), 
      process_manager_(process_manager), gateways_(gateways) {
    port_ = config_->gateway.http_port;
    
    InitializeServer();
    InitializeMonitoring();
    
    // Phase 2.2: 初始化异步流启动队列
    stream_start_queue_ = std::make_shared<streaming::StreamStartQueue>(4, 100);
    ::utils::Logger::Get()->info("Phase 2.2: StreamStartQueue initialized with 4 workers, max 100 queue");
    
    InitializeHandlers();
    
    // 初始化 ZLM 在线状态缓存
    last_zlm_check_time_ = std::chrono::steady_clock::now();
    cached_zlm_online_ = false;
}

bool HttpServer::Start() {
    if (is_running_) {
        ::utils::Logger::Get()->warn("HTTP server is already running");
        return false;
    }

    // 检查端口占用并尝试清理（防止重启失败）
    ::utils::SystemUtils::CheckAndCleanPort(port_);

    RegisterRoutes();

    // 在后台线程启动服务器
    server_thread_ = std::thread([this]() {
        ::utils::Logger::Get()->info("启动 HTTP API 服务器，端口: {}", port_);
        if (!server_->listen("0.0.0.0", port_)) {
            ::utils::Logger::Get()->error("HTTP 服务器启动失败，端口: {}", port_);
            is_running_ = false;
        }
    });

    // 等待服务器启动
    std::this_thread::sleep_for(std::chrono::milliseconds(time::HTTP_SERVER_START_WAIT_MS));
    is_running_ = true;

    ::utils::Logger::Get()->info("HTTP API server started: http://0.0.0.0:{}", port_);
    return true;
}

void HttpServer::Stop() {
    if (!is_running_) {
        return;
    }

    ::utils::Logger::Get()->info("Stopping HTTP API server");
    
    // Stop the server (breaks the listen loop)
    server_->stop();
    
    // Wait for the thread to exit
    if (server_thread_.joinable()) {
        server_thread_.join();
    }
    
    is_running_ = false;
}

void HttpServer::RegisterRoutes() {
    using namespace api::utils;
    
    // 健康检查
    RouteHelper::RegisterGet(server_.get(), "/health", 
        [this](const httplib::Request& req, httplib::Response& res) {
            HandleHealth(req, res);
        });

    // API 路由 - 流管理
    RouteHelper::RegisterGet(server_.get(), "/api/v1/streams",
        [this](const httplib::Request& req, httplib::Response& res) {
            stream_handler_->HandleGetStreams(req, res);
        });

    // POST /api/v1/streams - 创建新流（通用API）
    RouteHelper::RegisterPost(server_.get(), "/api/v1/streams",
        [this](const httplib::Request& req, httplib::Response& res) {
            stream_handler_->HandleStartStream(req, res);
        });

    RouteHelper::RegisterPost(server_.get(), "/api/v1/streams/start",
        [this](const httplib::Request& req, httplib::Response& res) {
            stream_handler_->HandleStartStream(req, res);
        });

    RouteHelper::RegisterPost(server_.get(), "/api/v1/streams/stop",
        [this](const httplib::Request& req, httplib::Response& res) {
            stream_handler_->HandleStopStream(req, res);
        });

    RouteHelper::RegisterDelete(server_.get(), "/api/v1/streams/:app/:stream",
        [this](const httplib::Request& req, httplib::Response& res) {
            stream_handler_->HandleStopStream(req, res);
        });

    RouteHelper::RegisterGet(server_.get(), "/api/v1/streams/:app/:stream",
        [this](const httplib::Request& req, httplib::Response& res) {
            stream_handler_->HandleGetStreamInfo(req, res);
        });

    // ONVIF 设备管理 API
    RouteHelper::RegisterPost(server_.get(), "/api/v1/devices/discover",
        [this](const httplib::Request& req, httplib::Response& res) {
            device_handler_->HandleDiscoverDevices(req, res);
        });

    RouteHelper::RegisterGet(server_.get(), "/api/v1/devices",
        [this](const httplib::Request& req, httplib::Response& res) {
            device_handler_->HandleGetDevices(req, res);
        });

    RouteHelper::RegisterGet(server_.get(), "/api/v1/devices/:id",
        [this](const httplib::Request& req, httplib::Response& res) {
            device_handler_->HandleGetDevice(req, res);
        });

    RouteHelper::RegisterGet(server_.get(), "/api/v1/devices/:id/rtsp-urls",
        [this](const httplib::Request& req, httplib::Response& res) {
            device_handler_->HandleGetDeviceRTSPURLs(req, res);
        });

    RouteHelper::RegisterPost(server_.get(), "/api/v1/devices/add",
        [this](const httplib::Request& req, httplib::Response& res) {
            device_handler_->HandleAddDevice(req, res);
        });

    RouteHelper::RegisterDelete(server_.get(), "/api/v1/devices/:device_id",
        [this](const httplib::Request& req, httplib::Response& res) {
            device_handler_->HandleDeleteDevice(req, res);
        });

    // ISAPI 设备管理 API
    RouteHelper::RegisterPost(server_.get(), "/api/v1/isapi/devices/discover",
        [this](const httplib::Request& req, httplib::Response& res) {
            device_handler_->HandleDiscoverISAPIDevices(req, res);
        });

    RouteHelper::RegisterGet(server_.get(), "/api/v1/isapi/devices",
        [this](const httplib::Request& req, httplib::Response& res) {
            device_handler_->HandleGetISAPIDevices(req, res);
        });

    RouteHelper::RegisterGet(server_.get(), "/api/v1/isapi/devices/:id",
        [this](const httplib::Request& req, httplib::Response& res) {
            device_handler_->HandleGetISAPIDevice(req, res);
        });

    RouteHelper::RegisterGet(server_.get(), "/api/v1/isapi/devices/:id/rtsp-urls",
        [this](const httplib::Request& req, httplib::Response& res) {
            device_handler_->HandleGetISAPIDeviceRTSPURLs(req, res);
        });

    RouteHelper::RegisterPost(server_.get(), "/api/v1/isapi/devices/add",
        [this](const httplib::Request& req, httplib::Response& res) {
            device_handler_->HandleAddISAPIDevice(req, res);
        });

    RouteHelper::RegisterDelete(server_.get(), "/api/v1/isapi/devices/:device_id",
        [this](const httplib::Request& req, httplib::Response& res) {
            device_handler_->HandleDeleteISAPIDevice(req, res);
        });

    // 大华设备管理 API
    RouteHelper::RegisterPost(server_.get(), "/api/v1/dahua/devices/discover",
        [this](const httplib::Request& req, httplib::Response& res) {
            device_handler_->HandleDiscoverDahuaDevices(req, res);
        });

    RouteHelper::RegisterGet(server_.get(), "/api/v1/dahua/devices",
        [this](const httplib::Request& req, httplib::Response& res) {
            device_handler_->HandleGetDahuaDevices(req, res);
        });

    RouteHelper::RegisterGet(server_.get(), "/api/v1/dahua/devices/:id",
        [this](const httplib::Request& req, httplib::Response& res) {
            device_handler_->HandleGetDahuaDevice(req, res);
        });

    RouteHelper::RegisterGet(server_.get(), "/api/v1/dahua/devices/:id/rtsp-urls",
        [this](const httplib::Request& req, httplib::Response& res) {
            device_handler_->HandleGetDahuaDeviceRTSPURLs(req, res);
        });

    RouteHelper::RegisterPost(server_.get(), "/api/v1/dahua/devices/add",
        [this](const httplib::Request& req, httplib::Response& res) {
            device_handler_->HandleAddDahuaDevice(req, res);
        });

    RouteHelper::RegisterDelete(server_.get(), "/api/v1/dahua/devices/:device_id",
        [this](const httplib::Request& req, httplib::Response& res) {
            device_handler_->HandleDeleteDahuaDevice(req, res);
        });

    // PSIA 设备管理 API
    RouteHelper::RegisterPost(server_.get(), "/api/v1/psia/devices/discover",
        [this](const httplib::Request& req, httplib::Response& res) {
            device_handler_->HandleDiscoverPSIADevices(req, res);
        });

    RouteHelper::RegisterGet(server_.get(), "/api/v1/psia/devices",
        [this](const httplib::Request& req, httplib::Response& res) {
            device_handler_->HandleGetPSIADevices(req, res);
        });

    RouteHelper::RegisterGet(server_.get(), "/api/v1/psia/devices/:id",
        [this](const httplib::Request& req, httplib::Response& res) {
            device_handler_->HandleGetPSIADevice(req, res);
        });

    RouteHelper::RegisterGet(server_.get(), "/api/v1/psia/devices/:id/rtsp-urls",
        [this](const httplib::Request& req, httplib::Response& res) {
            device_handler_->HandleGetPSIADeviceRTSPURLs(req, res);
        });

    RouteHelper::RegisterPost(server_.get(), "/api/v1/psia/devices/add",
        [this](const httplib::Request& req, httplib::Response& res) {
            device_handler_->HandleAddPSIADevice(req, res);
        });

    RouteHelper::RegisterDelete(server_.get(), "/api/v1/psia/devices/:device_id",
        [this](const httplib::Request& req, httplib::Response& res) {
            device_handler_->HandleDeletePSIADevice(req, res);
        });

    // 本地摄像头设备管理 API
    RouteHelper::RegisterPost(server_.get(), "/api/v1/local-camera/devices/discover",
        [this](const httplib::Request& req, httplib::Response& res) {
            device_handler_->HandleDiscoverLocalCameras(req, res);
        });

    // 刷新本地摄像头设备（强制重新扫描）
    RouteHelper::RegisterPost(server_.get(), "/api/v1/local-camera/devices/refresh",
        [this](const httplib::Request& req, httplib::Response& res) {
            device_handler_->HandleRefreshLocalCameras(req, res);
        });

    RouteHelper::RegisterGet(server_.get(), "/api/v1/local-camera/devices",
        [this](const httplib::Request& req, httplib::Response& res) {
            device_handler_->HandleGetLocalCameras(req, res);
        });

    RouteHelper::RegisterGet(server_.get(), "/api/v1/local-camera/devices/:id/capabilities",
        [this](const httplib::Request& req, httplib::Response& res) {
            device_handler_->HandleGetLocalCameraCapabilities(req, res);
        });
    
    // GB28181 设备管理 API
    if (gateways_.Get<gateway::GB28181Gateway>("gb28181")) {
        RouteHelper::RegisterGet(server_.get(), "/api/v1/gb28181/devices",
            [this](const httplib::Request& req, httplib::Response& res) {
                device_handler_->HandleGB28181GetDevices(req, res);
            });

        RouteHelper::RegisterGet(server_.get(), "/api/v1/gb28181/devices/:id",
            [this](const httplib::Request& req, httplib::Response& res) {
                device_handler_->HandleGB28181GetDevice(req, res);
            });

        RouteHelper::RegisterPost(server_.get(), "/api/v1/gb28181/devices/add",
            [this](const httplib::Request& req, httplib::Response& res) {
                device_handler_->HandleGB28181AddDevice(req, res);
            });

        RouteHelper::RegisterDelete(server_.get(), "/api/v1/gb28181/devices/:device_id",
            [this](const httplib::Request& req, httplib::Response& res) {
                device_handler_->HandleGB28181DeleteDevice(req, res);
            });

        RouteHelper::RegisterPost(server_.get(), "/api/v1/gb28181/streams/start",
            [this](const httplib::Request& req, httplib::Response& res) {
                stream_handler_->HandleGB28181StartStream(req, res);
            });

        RouteHelper::RegisterPost(server_.get(), "/api/v1/gb28181/streams/stop",
            [this](const httplib::Request& req, httplib::Response& res) {
                stream_handler_->HandleGB28181StopStream(req, res);
            });
    }
    
    // Local Camera 设备管理 API
    RouteHelper::RegisterPost(server_.get(), "/api/v1/local-camera/devices/discover",
        [this](const httplib::Request& req, httplib::Response& res) {
            device_handler_->HandleDiscoverLocalCameras(req, res);
        });

    RouteHelper::RegisterGet(server_.get(), "/api/v1/local-camera/devices",
        [this](const httplib::Request& req, httplib::Response& res) {
            device_handler_->HandleGetLocalCameras(req, res);
        });

    RouteHelper::RegisterPost(server_.get(), "/api/v1/local-camera/devices/refresh",
        [this](const httplib::Request& req, httplib::Response& res) {
            device_handler_->HandleRefreshLocalCameras(req, res);
        });

    RouteHelper::RegisterGet(server_.get(), "/api/v1/local-camera/devices/:id/capabilities",
        [this](const httplib::Request& req, httplib::Response& res) {
            device_handler_->HandleGetLocalCameraCapabilities(req, res);
        });

    RouteHelper::RegisterGet(server_.get(), "/api/v1/local-camera/devices/:id",
        [this](const httplib::Request& req, httplib::Response& res) {
            device_handler_->HandleGetLocalCamera(req, res);
        });

    RouteHelper::RegisterPost(server_.get(), "/api/v1/local-camera/streams/start",
        [this](const httplib::Request& req, httplib::Response& res) {
            device_handler_->HandleStartLocalCameraStream(req, res);
        });

    RouteHelper::RegisterPost(server_.get(), "/api/v1/local-camera/streams/stop",
        [this](const httplib::Request& req, httplib::Response& res) {
            device_handler_->HandleStopLocalCameraStream(req, res);
        });

// Process Manager API
    RouteHelper::RegisterGet(server_.get(), "/api/v1/processes",
        [this](const httplib::Request& req, httplib::Response& res) {
            process_handler_->HandleGetProcesses(req, res);
        });

    RouteHelper::RegisterGet(server_.get(), "/api/v1/processes/:id",
        [this](const httplib::Request& req, httplib::Response& res) {
            process_handler_->HandleGetProcess(req, res);
        });

    // ==========================================
    // 监控和统计 API
    // ==========================================
    RouteHelper::RegisterGet(server_.get(), "/api/v1/monitoring/system",
        [this](const httplib::Request& req, httplib::Response& res) {
            statistics_handler_->HandleGetSystemStatistics(req, res);
        });

    RouteHelper::RegisterGet(server_.get(), "/api/v1/monitoring/streams",
        [this](const httplib::Request& req, httplib::Response& res) {
            statistics_handler_->HandleGetAllStreamStats(req, res);
        });

    RouteHelper::RegisterGet(server_.get(), "/api/v1/monitoring/streams/:app/:stream",
        [this](const httplib::Request& req, httplib::Response& res) {
            statistics_handler_->HandleGetStreamStats(req, res);
        });

    RouteHelper::RegisterGet(server_.get(), "/api/v1/monitoring/protocols",
        [this](const httplib::Request& req, httplib::Response& res) {
            statistics_handler_->HandleGetProtocolStatistics(req, res);
        });

    RouteHelper::RegisterGet(server_.get(), "/api/v1/monitoring/errors",
        [this](const httplib::Request& req, httplib::Response& res) {
            statistics_handler_->HandleGetErrorStatistics(req, res);
        });

    RouteHelper::RegisterGet(server_.get(), "/api/v1/monitoring/dashboard",
        [this](const httplib::Request& req, httplib::Response& res) {
            statistics_handler_->HandleGetDashboard(req, res);
        });

    RouteHelper::RegisterGet(server_.get(), "/api/v1/monitoring/hook-stats",
        [this](const httplib::Request& req, httplib::Response& res) {
            statistics_handler_->HandleGetHookStats(req, res);
        });

    RouteHelper::RegisterGet(server_.get(), "/api/v1/monitoring/status-statistics",
        [this](const httplib::Request& req, httplib::Response& res) {
            statistics_handler_->HandleGetStatusStatistics(req, res);
        });

    // ==========================================
    // ZLM Hook 接口（用于接收 ZLM 的实时通知）
    // ==========================================
    RouteHelper::RegisterPost(server_.get(), "/api/v1/hooks/stream_changed",
        [this](const httplib::Request& req, httplib::Response& res) {
            hook_handler_->HandleStreamChanged(req, res);
        });

    RouteHelper::RegisterPost(server_.get(), "/api/v1/hooks/stream_none_reader",
        [this](const httplib::Request& req, httplib::Response& res) {
            hook_handler_->HandleStreamNoneReader(req, res);
        });

    RouteHelper::RegisterPost(server_.get(), "/api/v1/hooks/play",
        [this](const httplib::Request& req, httplib::Response& res) {
            hook_handler_->HandlePlay(req, res);
        });

    RouteHelper::RegisterPost(server_.get(), "/api/v1/hooks/publish",
        [this](const httplib::Request& req, httplib::Response& res) {
            hook_handler_->HandlePublish(req, res);
        });

    RouteHelper::RegisterPost(server_.get(), "/api/v1/hooks/stream_not_found",
        [this](const httplib::Request& req, httplib::Response& res) {
            hook_handler_->HandleStreamNotFound(req, res);
        });

    // CORS 支持 + Phase 4 优化: Keep-Alive 连接配置
    server_->set_default_headers({
        {"Access-Control-Allow-Origin", "*"},
        {"Access-Control-Allow-Methods", "GET, POST, PUT, DELETE, OPTIONS"},
        {"Access-Control-Allow-Headers", "Content-Type, Authorization"},
        {"Connection", "keep-alive"},
        {"Keep-Alive", "timeout=10, max=100"}
    });

    server_->Options(".*", [](const httplib::Request&, httplib::Response& res) {
        res.status = 204;
    });
}

void HttpServer::HandleHealth(const httplib::Request& /* req */, httplib::Response& res) {
    // 健康检查应该快速响应，不阻塞
    // 使用缓存的 ZLM 在线状态，避免每次请求都调用 IsOnline()
    bool zlm_online = false;
    
    if (zlm_client_) {
        auto now = std::chrono::steady_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(
            now - last_zlm_check_time_).count();
        
        // 如果距离上次检查超过间隔时间，更新缓存
        if (elapsed >= ZLM_CHECK_INTERVAL_SECONDS) {
            std::lock_guard<std::mutex> lock(zlm_check_mutex_);
            // 再次检查时间（防止并发）
            elapsed = std::chrono::duration_cast<std::chrono::seconds>(
                now - last_zlm_check_time_).count();
            if (elapsed >= ZLM_CHECK_INTERVAL_SECONDS) {
                try {
                    cached_zlm_online_ = zlm_client_->IsOnline();
                    last_zlm_check_time_ = now;
                } catch (const std::exception& e) {
                    ::utils::Logger::Get()->warn("检查 ZLM 在线状态失败: {}", e.what());
                    // 使用缓存的旧值
                }
            }
            zlm_online = cached_zlm_online_.load();
        } else {
            // 使用缓存值
            zlm_online = cached_zlm_online_.load();
        }
    }
    
    json response = {
        {"status", "healthy"},
        {"service", "ZLM Gateway"},
        {"zlmediakit_online", zlm_online}
    };
    res.set_content(response.dump(), "application/json");
}

void HttpServer::InitializeServer() {
    server_ = std::make_unique<httplib::Server>();
    
    // Configure thread pool for concurrent request handling (fix single-threaded blocking)
    server_->new_task_queue = [] {
        return new httplib::ThreadPool(4);  // Use 4 threads
    };
    
    // 配置连接管理，防止连接泄漏
    // Phase 4 优化: 调整超时设置以优化 keep-alive 连接复用
    // 读超时：5秒 → 10秒（增加空闲连接保持时间）
    server_->set_read_timeout(10, 0);
    // 写超时：5秒 → 10秒（增加空闲连接保持时间）
    server_->set_write_timeout(10, 0);
    
    ::utils::Logger::Get()->info("HTTP 服务器配置 (Phase 4优化): 读超时=10s, 写超时=10s");
}

void HttpServer::InitializeMonitoring() {
    // 初始化统计管理器
    statistics_manager_ = std::make_shared<monitoring::StatisticsManager>(
        stream_manager_, process_manager_);
}

void HttpServer::InitializeHandlers() {
    // 初始化流管理handler
    // Phase 2.2: 传入 stream_start_queue_ 用于异步流启动
    // Phase 6.2: 传入 gateways_ 实例集合
    stream_handler_ = std::make_shared<handlers::StreamHandler>(
        zlm_client_, stream_manager_, process_manager_,
        gateways_, stream_start_queue_);
    
    // 初始化设备管理handler
    // Phase 6.2: 传入 gateways_ 实例集合
    device_handler_ = std::make_shared<handlers::DeviceHandler>(
        gateways_, stream_manager_);
    
    // 初始化 Hook handler（用于接收 ZLM 的 Hook 通知）
    // 传入 stream_handler 以支持按需拉流功能
    hook_handler_ = std::make_shared<handlers::HookHandler>(stream_manager_, stream_handler_);
    
    // 初始化统计监控handler（传入 hook_handler 和 stream_manager 以获取 Hook 统计信息和状态统计）
    statistics_handler_ = std::make_shared<handlers::StatisticsHandler>(statistics_manager_, hook_handler_, stream_manager_);
    
    // Initialize ProcessHandler
    process_handler_ = std::make_shared<handlers::ProcessHandler>(process_manager_);
}

// GB28181 设备管理 API 处理函数


} // namespace api

