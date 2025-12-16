#include "gateway/local_camera/health_monitor.hpp"
#include "config/constants.hpp"
#include "gateway/local_camera/local_camera_gateway.hpp"  // 需要 LocalCameraStreamInfo 的完整定义
#include "config/config_loader.hpp"  // 需要 Config 的完整定义
#include "process/process_monitor.hpp"
#include "process/process_manager.hpp"  // 需要 ProcessManager 和 ProcessStatus 的完整定义
#include "utils/logger.hpp"
#include "utils/zlm_stream_checker.hpp"
#include "api/websocket_server.hpp"
#include "streaming/stream_manager.hpp"
#include <nlohmann/json.hpp>

namespace gateway {
namespace local_camera {

HealthMonitor::HealthMonitor(std::shared_ptr<config::Config> config,
                            std::shared_ptr<streaming::ZLMClient> zlm_client,
                            std::shared_ptr<process::ProcessManager> process_manager)
    : config_(config), zlm_client_(zlm_client), process_manager_(process_manager),
      check_interval_seconds_(10) {
}

HealthMonitor::~HealthMonitor() {
    Stop();
}

void HealthMonitor::Start(GetStreamsCallback get_streams_callback,
                         HealthCheckCallback check_health_callback,
                         UpdateStreamStatusCallback update_status_callback,
                         UpdateRecoverInfoCallback update_recover_info_callback,
                         RecoverCallback recover_callback,
                         std::shared_ptr<api::WebSocketServer> websocket_server,
                         std::shared_ptr<streaming::StreamManager> stream_manager) {
    if (running_) {
        LOG_WARN("[HealthMonitor] 健康监控已在运行");
        return;
    }

    get_streams_callback_ = get_streams_callback;
    check_health_callback_ = check_health_callback;
    update_status_callback_ = update_status_callback;
    update_recover_info_callback_ = update_recover_info_callback;
    recover_callback_ = recover_callback;
    websocket_server_ = websocket_server;
    stream_manager_ = stream_manager;

    running_ = true;
    check_interval_seconds_ = config_->local_camera.health_monitor.check_interval_seconds;
    monitor_thread_ = std::thread(&HealthMonitor::MonitorThread, this);

    // 启动恢复线程清理任务（每30秒清理一次）
    std::thread cleanup_thread([this]() {
        while (running_) {
            std::this_thread::sleep_for(std::chrono::seconds(config::constants::time::HEALTH_CHECK_INTERVAL_SEC));
            if (running_) {
                CleanupRecoverThreads();
            }
        }
    });
    cleanup_thread.detach();

    LOG_INFO("[HealthMonitor] 健康监控已启动，检查间隔: {}秒，自动恢复: {}",
            check_interval_seconds_,
            config_->local_camera.health_monitor.auto_recover ? "启用" : "禁用");
}

void HealthMonitor::Stop() {
    if (!running_) {
        return;
    }

    running_ = false;
    if (monitor_thread_.joinable()) {
        monitor_thread_.join();
    }

    // 等待所有恢复线程完成
    CleanupRecoverThreads();

    LOG_INFO("[HealthMonitor] 健康监控已停止");
}

bool HealthMonitor::CheckStreamHealth(const std::string& stream_id, StreamInfo& info) const {
    // 检查1: 进程是否还在运行
    if (info.pid > 0) {
        if (!process::ProcessMonitor::IsProcessAlive(info.pid)) {
            LOG_WARN("[HealthMonitor] 流进程已退出: {}/{} (PID: {})",
                    info.target_app, info.target_stream, info.pid);
            return false;
        }
    } else if (process_manager_) {
        // 如果使用 ProcessManager，检查进程状态
        auto process_info = process_manager_->GetProcessInfo(stream_id);
        if (process_info.status == process::ProcessStatus::Error ||
            process_info.status == process::ProcessStatus::Stopped) {
            LOG_WARN("[HealthMonitor] ProcessManager 报告流进程异常: {}/{}",
                    info.target_app, info.target_stream);
            return false;
        }
    }

    // 检查2: ZLM 中流是否还活着
    if (zlm_client_) {
        if (!::utils::ZLMStreamChecker::IsStreamAlive(zlm_client_, info.target_app, info.target_stream)) {
            LOG_WARN("[HealthMonitor] ZLM 中流不存在或已停止: {}/{}",
                    info.target_app, info.target_stream);
            return false;
        }
    }

    return true;
}

void HealthMonitor::MonitorThread() {
    LOG_INFO("[HealthMonitor] 流健康监控线程启动");

    while (running_) {
        std::this_thread::sleep_for(std::chrono::seconds(check_interval_seconds_));

        if (!running_) {
            break;
        }

        // 获取需要检查的流列表
        if (!get_streams_callback_) {
            continue;
        }

        auto streams_to_check = get_streams_callback_();

        // 检查每个流的健康状态
        for (const auto& stream_pair : streams_to_check) {
            const std::string& stream_id = stream_pair.first;
            StreamInfo info = stream_pair.second;  // 复制以避免长时间持锁

            bool is_healthy = false;
            if (check_health_callback_) {
                is_healthy = check_health_callback_(stream_id, info);
            } else {
                is_healthy = CheckStreamHealth(stream_id, info);
            }

            if (!is_healthy) {
                LOG_WARN("[HealthMonitor] 流健康检查失败: {}/{}",
                        info.target_app, info.target_stream);

                // 更新流状态为错误
                if (update_status_callback_) {
                    update_status_callback_(stream_id, GatewayStatus::Error);
                }

                // 更新 StreamManager 中的流状态
                if (stream_manager_) {
                    stream_manager_->UpdateStreamStatus(info.target_app,
                                                       info.target_stream,
                                                       streaming::StreamStatus::Error);
                }

                // 通过 WebSocket 通知前端
                if (websocket_server_) {
                    nlohmann::json stream_data = {
                        {"protocol", "local-camera"},
                        {"app", info.target_app},
                        {"stream", info.target_stream},
                        {"status", "error"},
                        {"device_id", info.device_id}
                    };
                    websocket_server_->BroadcastStreamUpdate(stream_data);
                }

                // 自动恢复（如果启用）
                if (config_->local_camera.health_monitor.auto_recover && recover_callback_) {
                    auto now = std::chrono::system_clock::now();
                    auto cooldown = std::chrono::seconds(config_->local_camera.health_monitor.recover_cooldown_seconds);

                    // 检查冷却时间
                    if (info.last_recover_time.time_since_epoch().count() == 0 ||
                        (now - info.last_recover_time) >= cooldown) {

                        // 检查恢复次数限制
                        if (info.recover_attempts < config_->local_camera.health_monitor.max_recover_attempts) {
                            LOG_INFO("[HealthMonitor] 尝试自动恢复流: {}/{} (尝试 {}/{})",
                                    info.target_app, info.target_stream,
                                    info.recover_attempts + 1,
                                    config_->local_camera.health_monitor.max_recover_attempts);

                            // 更新恢复信息
                            if (update_recover_info_callback_) {
                                update_recover_info_callback_(stream_id,
                                                              info.recover_attempts + 1,
                                                              now);
                            }

                            // 在后台线程中恢复，避免阻塞健康监控
                            std::thread recover_thread([this, stream_id, info]() {
                                try {
                                    std::string source_url = "local-camera://device_id:" + info.device_id;
                                    bool success = this->recover_callback_(source_url,
                                                                          info.target_app,
                                                                          info.target_stream,
                                                                          info.output_protocol);

                                    // 恢复成功后通知前端
                                    if (success && this->websocket_server_) {
                                        nlohmann::json stream_data = {
                                            {"protocol", "local-camera"},
                                            {"app", info.target_app},
                                            {"stream", info.target_stream},
                                            {"status", "running"},
                                            {"device_id", info.device_id},
                                            {"recovered", true}
                                        };
                                        this->websocket_server_->BroadcastStreamUpdate(stream_data);
                                        LOG_INFO("[HealthMonitor] 流自动恢复成功并已通知前端: {}/{}",
                                                info.target_app, info.target_stream);
                                    } else if (!success) {
                                        LOG_WARN("[HealthMonitor] 流自动恢复失败: {}/{}",
                                                info.target_app, info.target_stream);
                                    }
                                } catch (const std::exception& e) {
                                    LOG_ERROR("[HealthMonitor] 流自动恢复时发生异常: {} ({}/{})",
                                            e.what(), info.target_app, info.target_stream);
                                }
                            });

                            // 将恢复线程添加到管理列表
                            {
                                std::lock_guard<std::mutex> lock(recover_threads_mutex_);
                                recover_threads_.push_back(std::move(recover_thread));
                            }
                        } else {
                            LOG_WARN("[HealthMonitor] 流恢复次数已达上限，停止自动恢复: {}/{} (尝试次数: {})",
                                    info.target_app, info.target_stream, info.recover_attempts);
                        }
                    } else {
                        LOG_DEBUG("[HealthMonitor] 流恢复冷却中，跳过本次恢复: {}/{}",
                                info.target_app, info.target_stream);
                    }
                }
            }
        }
    }

    LOG_INFO("[HealthMonitor] 流健康监控线程退出");
}

void HealthMonitor::CleanupRecoverThreads() {
    std::lock_guard<std::mutex> lock(recover_threads_mutex_);

    // 清理已完成的线程（join 并移除）
    for (auto it = recover_threads_.begin(); it != recover_threads_.end();) {
        if (it->joinable()) {
            // 使用 try-catch 避免 join 已完成的线程时出错
            try {
                // 简化处理：直接 join，如果线程已完成会立即返回
                // 注意：join() 会阻塞直到线程完成，但恢复操作通常很快（几秒内）
                it->join();
                it = recover_threads_.erase(it);
            } catch (const std::exception& e) {
                LOG_WARN("[HealthMonitor] 清理恢复线程时出错: {}", e.what());
                ++it;
            }
        } else {
            // 线程不可 join，直接移除
            it = recover_threads_.erase(it);
        }
    }

    // 限制恢复线程数量（避免无限增长）
    const size_t max_recover_threads = 10;
    while (recover_threads_.size() > max_recover_threads) {
        // 等待最老的线程完成
        auto oldest = recover_threads_.begin();
        if (oldest->joinable()) {
            try {
                oldest->join();
            } catch (const std::exception& e) {
                LOG_WARN("[HealthMonitor] 等待恢复线程完成时出错: {}", e.what());
            }
        }
        recover_threads_.erase(oldest);
    }
}

} // namespace local_camera
} // namespace gateway

