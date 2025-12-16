#ifndef API_HANDLERS_HOOK_HANDLER_HPP
#define API_HANDLERS_HOOK_HANDLER_HPP

#include <memory>
#include <httplib.h>
#include <string>
#include <queue>
#include <mutex>
#include <thread>
#include <atomic>
#include <chrono>
#include <map>

namespace streaming {
class StreamManager;
}

namespace api {
namespace handlers {

class StreamHandler;  // 前向声明

/**
 * @brief Hook 事件统计信息（用于返回，不包含 atomic）
 */
struct HookEventStats {
    int64_t total_count = 0;              // 总事件数
    int64_t success_count = 0;            // 成功处理数
    int64_t retry_count = 0;              // 重试数
    int64_t failed_count = 0;             // 失败数
    int64_t external_stream_count = 0;    // 外部推流数
    int64_t total_processing_time_ms = 0; // 总处理时间（毫秒）
    
    // 按事件类型统计
    int64_t stream_changed_count = 0;
    int64_t stream_none_reader_count = 0;
    int64_t play_count = 0;
    int64_t publish_count = 0;
};

/**
 * @brief Hook 事件统计信息（内部使用，包含 atomic）
 */
struct HookEventStatsInternal {
    std::atomic<int64_t> total_count{0};
    std::atomic<int64_t> success_count{0};
    std::atomic<int64_t> retry_count{0};
    std::atomic<int64_t> failed_count{0};
    std::atomic<int64_t> external_stream_count{0};
    std::atomic<int64_t> total_processing_time_ms{0};
    
    std::atomic<int64_t> stream_changed_count{0};
    std::atomic<int64_t> stream_none_reader_count{0};
    std::atomic<int64_t> play_count{0};
    std::atomic<int64_t> publish_count{0};
};

/**
 * @brief 重试任务
 */
struct RetryTask {
    std::string app;
    std::string stream;
    std::string schema;
    std::string event_type;  // "stream_changed", "stream_none_reader", etc.
    std::string request_body;  // 原始请求体（JSON）
    int retry_count;
    std::chrono::steady_clock::time_point retry_time;
    
    RetryTask(const std::string& a, const std::string& s, const std::string& sc,
              const std::string& et, const std::string& body, int rc = 0)
        : app(a), stream(s), schema(sc), event_type(et), request_body(body), retry_count(rc) {
        // 延迟重试：第一次 1 秒，第二次 2 秒，第三次 3 秒
        retry_time = std::chrono::steady_clock::now() + 
                     std::chrono::seconds(1 + retry_count);
    }
};

/**
 * @brief Hook 处理器
 * 
 * 处理 ZLMediaKit 的 Hook 通知，实时更新流状态
 * 替代轮询机制，提升实时性和性能
 * 
 * 特性：
 * - 重试机制：处理时序问题，流不存在时延迟重试
 * - 性能监控：统计处理时间和成功率
 * - 日志优化：区分外部推流和未注册流
 */
class HookHandler {
public:
    /**
     * @brief 构造函数
     * @param stream_manager 流管理器
     * @param stream_handler 流处理器（用于按需拉流）
     */
    explicit HookHandler(std::shared_ptr<streaming::StreamManager> stream_manager,
                        std::shared_ptr<StreamHandler> stream_handler = nullptr);

    /**
     * @brief 析构函数
     */
    ~HookHandler();

    /**
     * @brief 处理流状态变化 Hook
     * @param req HTTP 请求
     * @param res HTTP 响应
     */
    void HandleStreamChanged(const httplib::Request& req, httplib::Response& res);

    /**
     * @brief 处理无播放器流 Hook
     * @param req HTTP 请求
     * @param res HTTP 响应
     */
    void HandleStreamNoneReader(const httplib::Request& req, httplib::Response& res);

    /**
     * @brief 处理播放 Hook
     * @param req HTTP 请求
     * @param res HTTP 响应
     */
    void HandlePlay(const httplib::Request& req, httplib::Response& res);

    /**
     * @brief 处理推流 Hook
     * @param req HTTP 请求
     * @param res HTTP 响应
     */
    void HandlePublish(const httplib::Request& req, httplib::Response& res);

    /**
     * @brief 处理流未找到 Hook（按需拉流）
     * @param req HTTP 请求
     * @param res HTTP 响应
     */
    void HandleStreamNotFound(const httplib::Request& req, httplib::Response& res);

    /**
     * @brief 获取统计信息
     * @return 统计信息
     */
    HookEventStats GetStats() const;

private:
    std::shared_ptr<streaming::StreamManager> stream_manager_;
    std::shared_ptr<StreamHandler> stream_handler_;  // 用于按需拉流
    
    // 重试机制
    std::queue<RetryTask> retry_queue_;
    std::mutex retry_queue_mutex_;
    std::thread retry_thread_;
    std::atomic<bool> retry_running_{false};
    static constexpr int MAX_RETRY_COUNT = 2;  // 最多重试 2 次
    
    // 性能监控
    mutable HookEventStatsInternal stats_;
    mutable std::mutex stats_mutex_;

    /**
     * @brief 解析 ZLM Hook 请求中的流信息
     * @param req HTTP 请求
     * @param app 输出：应用名
     * @param stream 输出：流名
     * @param schema 输出：协议 schema
     * @return 是否成功解析
     */
    bool ParseStreamInfo(const httplib::Request& req, 
                        std::string& app, 
                        std::string& stream, 
                        std::string& schema);

    /**
     * @brief 将 ZLM 的 schema 映射到 Gateway 的 output_protocol
     * @param schema ZLM schema
     * @return Gateway output_protocol
     */
    std::string SchemaToOutputProtocol(const std::string& schema);

    /**
     * @brief 判断流是否应该是 Gateway 管理的
     * @param app 应用名
     * @param stream 流名
     * @return true 如果是 Gateway 管理的流，false 如果是外部推流
     */
    bool IsGatewayManagedStream(const std::string& app, const std::string& stream) const;

    /**
     * @brief 处理流状态变化（内部实现）
     * @param app 应用名
     * @param stream 流名
     * @param schema 协议 schema
     * @param body JSON 请求体
     * @param is_retry 是否是重试
     * @return true 如果成功处理，false 如果需要重试
     */
    bool ProcessStreamChanged(const std::string& app, const std::string& stream,
                             const std::string& schema, const std::string& body, bool is_retry = false);

    /**
     * @brief 添加重试任务
     * @param task 重试任务
     */
    void AddRetryTask(const RetryTask& task);

    /**
     * @brief 重试线程主循环
     */
    void RetryThreadLoop();

    /**
     * @brief 记录性能指标
     * @param event_type 事件类型
     * @param processing_time_ms 处理时间（毫秒）
     * @param success 是否成功
     * @param is_retry 是否是重试
     * @param is_external 是否是外部推流
     */
    void RecordStats(const std::string& event_type, int64_t processing_time_ms,
                    bool success, bool is_retry = false, bool is_external = false);

    /**
     * @brief 从流名推断 source_url 和 protocol（按需拉流）
     * @param app 应用名
     * @param stream 流名
     * @param schema 请求的协议 schema
     * @param source_url 输出：源流地址
     * @param protocol 输出：协议类型
     * @return 是否成功推断
     */
    bool InferSourceUrl(const std::string& app, const std::string& stream, 
                       const std::string& schema,
                       std::string& source_url, std::string& protocol);
};

} // namespace handlers
} // namespace api

#endif // API_HANDLERS_HOOK_HANDLER_HPP

