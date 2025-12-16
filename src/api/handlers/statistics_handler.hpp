#ifndef API_HANDLERS_STATISTICS_HANDLER_HPP
#define API_HANDLERS_STATISTICS_HANDLER_HPP

#include <httplib.h>
#include <memory>

namespace monitoring {
class StatisticsManager;
}

namespace streaming {
class StreamManager;
}

namespace api {
namespace handlers {
class HookHandler;
}

namespace handlers {

/**
 * @brief 统计监控请求处理器
 */
class StatisticsHandler {
public:
    explicit StatisticsHandler(std::shared_ptr<monitoring::StatisticsManager> statistics_manager,
                             std::shared_ptr<handlers::HookHandler> hook_handler = nullptr,
                             std::shared_ptr<streaming::StreamManager> stream_manager = nullptr);

    void HandleGetSystemStatistics(const httplib::Request& req, httplib::Response& res);
    void HandleGetAllStreamStats(const httplib::Request& req, httplib::Response& res);
    void HandleGetStreamStats(const httplib::Request& req, httplib::Response& res);
    void HandleGetProtocolStatistics(const httplib::Request& req, httplib::Response& res);
    void HandleGetErrorStatistics(const httplib::Request& req, httplib::Response& res);
    void HandleGetDashboard(const httplib::Request& req, httplib::Response& res);
    void HandleGetHookStats(const httplib::Request& req, httplib::Response& res);
    void HandleGetStatusStatistics(const httplib::Request& req, httplib::Response& res);

private:
    std::shared_ptr<monitoring::StatisticsManager> statistics_manager_;
    std::shared_ptr<handlers::HookHandler> hook_handler_;
    std::shared_ptr<streaming::StreamManager> stream_manager_;
};

} // namespace handlers
} // namespace api

#endif // API_HANDLERS_STATISTICS_HANDLER_HPP

