#include "api/handlers/statistics_handler.hpp"
#include "api/handlers/hook_handler.hpp"
#include "api/utils/response_helper.hpp"
#include "monitoring/statistics_manager.hpp"
#include "streaming/stream_manager.hpp"
#include "utils/logger.hpp"
#include <nlohmann/json.hpp>

using json = nlohmann::json;

namespace api {
namespace handlers {

StatisticsHandler::StatisticsHandler(std::shared_ptr<monitoring::StatisticsManager> statistics_manager,
                                     std::shared_ptr<handlers::HookHandler> hook_handler,
                                     std::shared_ptr<streaming::StreamManager> stream_manager)
    : statistics_manager_(statistics_manager), hook_handler_(hook_handler), stream_manager_(stream_manager) {
}

void StatisticsHandler::HandleGetSystemStatistics(const httplib::Request& /* req */, httplib::Response& res) {
    try {
        if (!statistics_manager_) {
            api::utils::ResponseHelper::Error(res, -1, "Statistics manager not initialized", 500);
            return;
        }

        auto stats = statistics_manager_->GetSystemStatistics();
        
        json response = {
            {"code", 0},
            {"data", {
                {"streams", {
                    {"total", stats.total_streams},
                    {"running", stats.running_streams},
                    {"stopped", stats.stopped_streams},
                    {"error", stats.error_streams},
                    {"starting", stats.starting_streams}
                }},
                {"processes", {
                    {"total", stats.total_processes},
                    {"running", stats.running_processes},
                    {"error", stats.error_processes},
                    {"restarting", stats.restarting_processes}
                }},
                {"resources", {
                    {"total_cpu_usage", stats.total_cpu_usage},
                    {"total_memory_usage", stats.total_memory_usage},
                    {"total_network_speed", stats.total_network_speed},
                    {"total_bytes", stats.total_bytes}
                }},
                {"viewers", {
                    {"total", stats.total_viewers}
                }},
                {"errors", {
                    {"total", stats.total_errors},
                    {"network", stats.network_errors},
                    {"protocol", stats.protocol_errors},
                    {"config", stats.config_errors},
                    {"auth", stats.auth_errors}
                }},
                {"timestamp", stats.timestamp}
            }}
        };
        
        api::utils::ResponseHelper::Success(res, response["data"]);
    } catch (const std::exception& e) {
        ::utils::Logger::Get()->error("Get system statistics failed: {}", e.what());
        api::utils::ResponseHelper::Error(res, e);
    }
}

void StatisticsHandler::HandleGetAllStreamStats(const httplib::Request& /* req */, httplib::Response& res) {
    try {
        if (!statistics_manager_) {
            api::utils::ResponseHelper::Error(res, -1, "Statistics manager not initialized", 500);
            return;
        }

        auto all_stats = statistics_manager_->GetAllStreamStats();
        
        json data = json::array();

        for (const auto& stats : all_stats) {
            json stream_stats = {
                {"app", stats.app},
                {"stream", stats.stream},
                {"protocol", stats.protocol},
                {"status", stats.status},
                {"cpu_usage", stats.cpu_usage},
                {"memory_usage", stats.memory_usage},
                {"bytes_speed", stats.bytes_speed},
                {"total_bytes", stats.total_bytes},
                {"reader_count", stats.reader_count},
                {"uptime", stats.uptime},
                {"restart_count", stats.restart_count},
                {"timestamp", stats.timestamp}
            };
            data.push_back(stream_stats);
        }
        
        api::utils::ResponseHelper::Success(res, data);
    } catch (const std::exception& e) {
        ::utils::Logger::Get()->error("Get all stream stats failed: {}", e.what());
        api::utils::ResponseHelper::Error(res, e);
    }
}

void StatisticsHandler::HandleGetStreamStats(const httplib::Request& req, httplib::Response& res) {
    try {
        if (!statistics_manager_) {
            api::utils::ResponseHelper::Error(res, -1, "Statistics manager not initialized", 500);
            return;
        }

        auto app_it = req.path_params.find("app");
        auto stream_it = req.path_params.find("stream");
        
        if (app_it == req.path_params.end() || stream_it == req.path_params.end()) {
            api::utils::ResponseHelper::BadRequest(res, "Missing app or stream parameter");
            return;
        }

        std::string app = app_it->second;
        std::string stream = stream_it->second;
        
        auto stats = statistics_manager_->GetStreamStats(app, stream);
        
        if (stats.app.empty()) {
            api::utils::ResponseHelper::NotFound(res, "Stream not found");
            return;
        }

        json data = {
            {"app", stats.app},
            {"stream", stats.stream},
            {"protocol", stats.protocol},
            {"status", stats.status},
            {"cpu_usage", stats.cpu_usage},
            {"memory_usage", stats.memory_usage},
            {"bytes_speed", stats.bytes_speed},
            {"total_bytes", stats.total_bytes},
            {"reader_count", stats.reader_count},
            {"uptime", stats.uptime},
            {"restart_count", stats.restart_count},
            {"timestamp", stats.timestamp}
        };
        
        api::utils::ResponseHelper::Success(res, data);
    } catch (const std::exception& e) {
        ::utils::Logger::Get()->error("Get stream stats failed: {}", e.what());
        api::utils::ResponseHelper::Error(res, e);
    }
}

void StatisticsHandler::HandleGetProtocolStatistics(const httplib::Request& /* req */, httplib::Response& res) {
    try {
        if (!statistics_manager_) {
            api::utils::ResponseHelper::Error(res, -1, "Statistics manager not initialized", 500);
            return;
        }

        auto protocol_stats = statistics_manager_->GetProtocolStatistics();
        
        json data = json::array();

        for (const auto& stats : protocol_stats) {
            json protocol_json = {
                {"protocol", stats.protocol},
                {"stream_count", stats.stream_count},
                {"running_count", stats.running_count},
                {"total_bytes", stats.total_bytes},
                {"bytes_speed", stats.bytes_speed},
                {"total_viewers", stats.total_viewers}
            };
            data.push_back(protocol_json);
        }
        
        api::utils::ResponseHelper::Success(res, data);
    } catch (const std::exception& e) {
        ::utils::Logger::Get()->error("Get protocol statistics failed: {}", e.what());
        api::utils::ResponseHelper::Error(res, e);
    }
}

void StatisticsHandler::HandleGetErrorStatistics(const httplib::Request& /* req */, httplib::Response& res) {
    try {
        if (!statistics_manager_) {
            api::utils::ResponseHelper::Error(res, -1, "Statistics manager not initialized", 500);
            return;
        }

        auto error_stats = statistics_manager_->GetErrorStatistics();
        
        json data = json::object();

        for (const auto& pair : error_stats) {
            data[pair.first] = pair.second;
        }
        
        api::utils::ResponseHelper::Success(res, data);
    } catch (const std::exception& e) {
        ::utils::Logger::Get()->error("Get error statistics failed: {}", e.what());
        api::utils::ResponseHelper::Error(res, e);
    }
}

void StatisticsHandler::HandleGetDashboard(const httplib::Request& /* req */, httplib::Response& res) {
    try {
        if (!statistics_manager_) {
            // 返回空数据而不是错误，避免前端显示错误
            json response = {
                {"code", 0},
                {"data", {
                    {"system_stats", {
                        {"total_streams", 0},
                        {"running_streams", 0},
                        {"error_streams", 0},
                        {"total_processes", 0},
                        {"system_cpu_usage", 0},
                        {"system_memory_usage", 0},
                        {"system_total_memory", 0},
                        {"network_in_bytes", 0},
                        {"network_out_bytes", 0},
                        {"uptime_seconds", 0}
                    }},
                    {"protocol_stats", json::array()},
                    {"streams_summary", json::array()},
                    {"error_stats", json::object()}
                }}
            };
            res.set_content(response.dump(), "application/json");
            return;
        }

        // 获取所有统计数据
        auto system_stats = statistics_manager_->GetSystemStatistics();
        auto stream_stats = statistics_manager_->GetAllStreamStats();
        auto protocol_stats = statistics_manager_->GetProtocolStatistics();
        auto error_stats = statistics_manager_->GetErrorStatistics();
        
        json response = {
            {"code", 0},
            {"data", {
                {"system", {
                    {"streams", {
                        {"total", system_stats.total_streams},
                        {"running", system_stats.running_streams},
                        {"stopped", system_stats.stopped_streams},
                        {"error", system_stats.error_streams},
                        {"starting", system_stats.starting_streams}
                    }},
                    {"processes", {
                        {"total", system_stats.total_processes},
                        {"running", system_stats.running_processes},
                        {"error", system_stats.error_processes},
                        {"restarting", system_stats.restarting_processes}
                    }},
                    {"resources", {
                        {"total_cpu_usage", system_stats.total_cpu_usage},
                        {"total_memory_usage", system_stats.total_memory_usage},
                        {"total_network_speed", system_stats.total_network_speed},
                        {"total_bytes", system_stats.total_bytes}
                    }},
                    {"viewers", {
                        {"total", system_stats.total_viewers}
                    }},
                    {"errors", {
                        {"total", system_stats.total_errors},
                        {"network", system_stats.network_errors},
                        {"protocol", system_stats.protocol_errors},
                        {"config", system_stats.config_errors},
                        {"auth", system_stats.auth_errors}
                    }},
                    {"timestamp", system_stats.timestamp}
                }},
                {"protocols", json::array()},
                {"streams", json::array()},
                {"errors", json::object()}
            }}
        };

        // 添加协议统计
        for (const auto& stats : protocol_stats) {
            json protocol_json = {
                {"protocol", stats.protocol},
                {"stream_count", stats.stream_count},
                {"running_count", stats.running_count},
                {"total_bytes", stats.total_bytes},
                {"bytes_speed", stats.bytes_speed},
                {"total_viewers", stats.total_viewers}
            };
            response["data"]["protocols"].push_back(protocol_json);
        }

        // 添加流统计摘要（限制数量）
        size_t max_streams = 50;
        size_t count = 0;
        for (const auto& stats : stream_stats) {
            if (count >= max_streams) {
                break;
            }
            json stream_json = {
                {"app", stats.app},
                {"stream", stats.stream},
                {"protocol", stats.protocol},
                {"status", stats.status},
                {"cpu_usage", stats.cpu_usage},
                {"memory_usage", stats.memory_usage},
                {"bytes_speed", stats.bytes_speed},
                {"reader_count", stats.reader_count}
            };
            response["data"]["streams"].push_back(stream_json);
            count++;
        }

        // 添加错误统计
        for (const auto& pair : error_stats) {
            response["data"]["errors"][pair.first] = pair.second;
        }

        // 添加 Hook 统计信息（如果可用）
        if (hook_handler_) {
            auto hook_stats = hook_handler_->GetStats();
            response["data"]["hook_stats"] = {
                {"total_count", hook_stats.total_count},
                {"success_count", hook_stats.success_count},
                {"retry_count", hook_stats.retry_count},
                {"failed_count", hook_stats.failed_count},
                {"external_stream_count", hook_stats.external_stream_count},
                {"avg_processing_time_ms", hook_stats.total_count > 0 
                    ? (hook_stats.total_processing_time_ms / hook_stats.total_count) : 0},
                {"success_rate", hook_stats.total_count > 0 
                    ? ((double)hook_stats.success_count / hook_stats.total_count * 100) : 0},
                {"events", {
                    {"stream_changed", hook_stats.stream_changed_count},
                    {"stream_none_reader", hook_stats.stream_none_reader_count},
                    {"play", hook_stats.play_count},
                    {"publish", hook_stats.publish_count}
                }}
            };
        }
        
        res.set_content(response.dump(), "application/json");
    } catch (const std::exception& e) {
        ::utils::Logger::Get()->error("Get dashboard failed: {}", e.what());
        json error = {
            {"code", -1},
            {"msg", e.what()}
        };
        res.status = 500;
        res.set_content(error.dump(), "application/json");
    }
}

void StatisticsHandler::HandleGetHookStats(const httplib::Request& /* req */, httplib::Response& res) {
    try {
        if (!hook_handler_) {
            api::utils::ResponseHelper::Error(res, -1, "Hook handler not initialized", 500);
            return;
        }

        auto stats = hook_handler_->GetStats();
        
        json data = {
            {"total_count", stats.total_count},
            {"success_count", stats.success_count},
            {"retry_count", stats.retry_count},
            {"failed_count", stats.failed_count},
            {"external_stream_count", stats.external_stream_count},
            {"total_processing_time_ms", stats.total_processing_time_ms},
            {"avg_processing_time_ms", stats.total_count > 0 
                ? (stats.total_processing_time_ms / stats.total_count) : 0},
            {"success_rate", stats.total_count > 0 
                ? ((double)stats.success_count / stats.total_count * 100) : 0},
            {"events", {
                {"stream_changed", stats.stream_changed_count},
                {"stream_none_reader", stats.stream_none_reader_count},
                {"play", stats.play_count},
                {"publish", stats.publish_count}
            }}
        };
        
        api::utils::ResponseHelper::Success(res, data);
    } catch (const std::exception& e) {
        ::utils::Logger::Get()->error("Get hook stats failed: {}", e.what());
        api::utils::ResponseHelper::Error(res, e);
    }
}

void StatisticsHandler::HandleGetStatusStatistics(const httplib::Request& /* req */, httplib::Response& res) {
    try {
        if (!stream_manager_) {
            api::utils::ResponseHelper::Error(res, -1, "Stream manager not initialized", 500);
            return;
        }

        auto stats = stream_manager_->GetStatusStatistics();
        api::utils::ResponseHelper::Success(res, stats);
    } catch (const std::exception& e) {
        ::utils::Logger::Get()->error("Get status statistics failed: {}", e.what());
        api::utils::ResponseHelper::Error(res, e);
    }
}

} // namespace handlers
} // namespace api

