#include "streaming/stream_start_queue.hpp"
#include "utils/logger.hpp"
#include "config/constants.hpp"
#include <chrono>

namespace streaming {

StreamStartQueue::StreamStartQueue(size_t worker_count, size_t max_queue_size)
    : max_queue_size_(max_queue_size) {
    
    LOG_INFO("StreamStartQueue initializing with {} workers, max queue size: {}", 
             worker_count, max_queue_size);
    
    // 启动worker线程
    workers_.reserve(worker_count);
    for (size_t i = 0; i < worker_count; ++i) {
        workers_.emplace_back(&StreamStartQueue::WorkerLoop, this);
    }
    
    LOG_INFO("StreamStartQueue initialized successfully");
}

StreamStartQueue::~StreamStartQueue() {
    Stop();
}

bool StreamStartQueue::Enqueue(StartTask task) {
    {
        std::lock_guard<std::mutex> lock(queue_mutex_);
        
        // 检查队列是否已满
        if (queue_.size() >= max_queue_size_) {
            LOG_WARN("StreamStartQueue is full, rejecting task for {}/{}", 
                    task.app, task.stream);
            return false;
        }
        
        queue_.push(std::move(task));
        pending_count_++;
    }
    
    cv_.notify_one();
    return true;
}

void StreamStartQueue::Stop() {
    if (!running_.load()) {
        return;  // 已经停止
    }
    
    LOG_INFO("Stopping StreamStartQueue...");
    
    running_ = false;
    cv_.notify_all();
    
    // 等待所有worker完成
    for (auto& worker : workers_) {
        if (worker.joinable()) {
            worker.join();
        }
    }
    
    LOG_INFO("StreamStartQueue stopped. Completed: {}, Failed: {}", 
             completed_count_.load(), failed_count_.load());
}

void StreamStartQueue::WorkerLoop() {
    LOG_DEBUG("StreamStartQueue worker thread started");
    
    while (running_.load()) {
        StartTask task;
        
        {
            std::unique_lock<std::mutex> lock(queue_mutex_);
            
            // 等待任务或停止信号
            cv_.wait(lock, [this] {
                return !queue_.empty() || !running_.load();
            });
            
            // 检查是否应该退出
            if (!running_.load() && queue_.empty()) {
                break;
            }
            
            if (queue_.empty()) {
                continue;
            }
            
            // 取出任务
            task = std::move(queue_.front());
            queue_.pop();
            pending_count_--;
        }
        
        // 执行任务（在锁外）
        ExecuteTask(task);
    }
    
    LOG_DEBUG("StreamStartQueue worker thread stopped");
}

void StreamStartQueue::ExecuteTask(StartTask& task) {
    auto start_time = std::chrono::steady_clock::now();
    
    LOG_INFO("Executing stream start task: {}/{} (protocol: {})", 
            task.app, task.stream, task.protocol);
    
    bool success = false;
    std::string error_message;
    
    try {
        // Phase 2.2: 执行Gateway启动逻辑（由Handler提供的execute_func）
        if (task.execute_func) {
            auto result = task.execute_func();
            success = result.IsSuccess();
            if (!success) {
                error_message = result.Error().what();
                LOG_ERROR("Stream start task execution failed: {}/{} - {}", task.app, task.stream, error_message);
            }
        } else {
            error_message = "Missing execute function";
            LOG_ERROR("Stream start task missing execute_func: {}/{}", task.app, task.stream);
        }
        
        // 如果启动成功，等待FFmpeg进程稳定
        if (success) {
            std::this_thread::sleep_for(
                std::chrono::milliseconds(config::constants::time::PROCESS_STABLE_WAIT_MS)
            );
        }
        
    } catch (const std::exception& e) {
        success = false;
        error_message = std::string("Exception: ") + e.what();
        
        LOG_ERROR("Stream start task failed: {}/{} - {}", 
                 task.app, task.stream, error_message);
    }
    
    // 更新统计
    if (success) {
        completed_count_++;
    } else {
        failed_count_++;
    }
    
    // 执行回调
    if (task.callback) {
        try {
            task.callback(success, error_message);
        } catch (const std::exception& e) {
            LOG_ERROR("Stream start callback failed: {}", e.what());
        }
    }
    
    auto elapsed = std::chrono::steady_clock::now() - start_time;
    auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count();
    
    LOG_INFO("Stream start task completed: {}/{} in {}ms (success: {})", 
            task.app, task.stream, elapsed_ms, success);
}

} // namespace streaming
