#ifndef STREAMING_STREAM_START_QUEUE_HPP
#define STREAMING_STREAM_START_QUEUE_HPP

#include <string>
#include <vector>
#include <queue>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <atomic>
#include <functional>
#include <nlohmann/json.hpp>
#include "common/result.hpp"

namespace streaming {

class StreamStartQueue {
public:
    struct StartTask {
        std::string app;
        std::string stream;
        std::string protocol;
        std::string source_url;
        std::string output_protocol;
        nlohmann::json config;
        
        // Return Result<void> instead of bool
        std::function<gateway::Result<void>()> execute_func;
        std::function<void(bool success, const std::string& error)> callback;
    };

    StreamStartQueue(size_t worker_count = 4, size_t max_queue_size = 1000);
    ~StreamStartQueue();

    bool Enqueue(StartTask task);
    void Stop();

private:
    void WorkerLoop();
    void ExecuteTask(StartTask& task);

    size_t max_queue_size_;
    std::queue<StartTask> queue_;
    std::mutex queue_mutex_;
    std::condition_variable cv_;
    std::vector<std::thread> workers_;
    std::atomic<bool> running_{true};
    
    std::atomic<size_t> pending_count_{0};
    std::atomic<size_t> completed_count_{0};
    std::atomic<size_t> failed_count_{0};
};

} // namespace streaming

#endif // STREAMING_STREAM_START_QUEUE_HPP
