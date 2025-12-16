#include "monitoring/statistics_manager.hpp"
#include "streaming/stream_manager.hpp"
#include "process/process_manager.hpp"
#include "process/process_monitor.hpp"
#include "utils/logger.hpp"
#include <chrono>
#include <algorithm>

namespace monitoring {

StatisticsManager::StatisticsManager(std::shared_ptr<streaming::StreamManager> stream_manager,
                                     std::shared_ptr<process::ProcessManager> process_manager)
    : stream_manager_(stream_manager), process_manager_(process_manager) {
}

SystemStatistics StatisticsManager::GetSystemStatistics() {
    std::lock_guard<std::mutex> lock(mutex_);
    
    SystemStatistics stats;
    stats.timestamp = std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    
    if (!stream_manager_) {
        return stats;
    }
    
    // 获取所有流
    auto all_streams = stream_manager_->GetAllStreams();
    stats.total_streams = static_cast<int>(all_streams.size());
    
    // 统计流状态
    for (const auto& stream : all_streams) {
        switch (stream.status) {
            case streaming::StreamStatus::Running:
                stats.running_streams++;
                stats.total_viewers += stream.reader_count;
                stats.total_network_speed += stream.bytes_speed;
                stats.total_bytes += stream.total_bytes;
                break;
            case streaming::StreamStatus::Stopped:
                stats.stopped_streams++;
                break;
            case streaming::StreamStatus::Error:
                stats.error_streams++;
                stats.total_errors++;
                break;
            case streaming::StreamStatus::Starting:
                stats.starting_streams++;
                break;
            default:
                break;
        }
    }
    
    // 获取系统资源使用情况
    // TEMPORARY FIX: Commented out to resolve Gateway deadlock issue
    // auto system_resources = process::ProcessMonitor::GetSystemResources();
    // stats.total_cpu_usage = system_resources.cpu_usage;
    // stats.total_memory_usage = system_resources.memory_usage;

    // 获取进程统计（如果 ProcessManager 可用）
    if (process_manager_) {
        // 使用高效的统计接口，避免复制所有 ProcessInfo 对象导致崩溃
        auto proc_stats = process_manager_->GetProcessStatistics();
        
        stats.total_processes = proc_stats.total_processes;
        stats.running_processes = proc_stats.running_processes;
        stats.error_processes = proc_stats.error_processes;
        stats.restarting_processes = proc_stats.restarting_processes;
        
        stats.network_errors = proc_stats.network_errors;
        stats.protocol_errors = proc_stats.protocol_errors;
        stats.config_errors = proc_stats.config_errors;
        stats.auth_errors = proc_stats.auth_errors;
    }
    
    return stats;
}

std::vector<StreamPerformanceStats> StatisticsManager::GetAllStreamStats() {
    std::lock_guard<std::mutex> lock(mutex_);
    
    std::vector<StreamPerformanceStats> result;
    
    if (!stream_manager_) {
        return result;
    }
    
    auto all_streams = stream_manager_->GetAllStreams();
    auto now = std::chrono::system_clock::now();
    int64_t timestamp = std::chrono::duration_cast<std::chrono::seconds>(
        now.time_since_epoch()).count();
    
    for (const auto& stream : all_streams) {
        StreamPerformanceStats stats;
        stats.app = stream.app;
        stats.stream = stream.stream;
        stats.protocol = stream.protocol;
        stats.cpu_usage = 0.0;
        stats.memory_usage = 0;
        stats.bytes_speed = stream.bytes_speed;
        stats.total_bytes = stream.total_bytes;
        stats.reader_count = stream.reader_count;
        
        // 状态文本
        switch (stream.status) {
            case streaming::StreamStatus::Stopped:
                stats.status = "stopped";
                break;
            case streaming::StreamStatus::Starting:
                stats.status = "starting";
                break;
            case streaming::StreamStatus::Running:
                stats.status = "running";
                break;
            case streaming::StreamStatus::Stopping:
                stats.status = "stopping";
                break;
            case streaming::StreamStatus::Error:
                stats.status = "error";
                break;
        }
        
        // 计算运行时间
        if (stream.create_time > 0) {
            stats.uptime = timestamp - stream.create_time;
        }
        
        // 从 ProcessManager 获取进程资源使用（如果有 PID）
        if (process_manager_ && stream.pid > 0) {
            auto process_info = process_manager_->GetProcessInfo(stream.app + "/" + stream.stream);
            if (process_info.status != process::ProcessStatus::Stopped || process_info.pid > 0) {
                stats.cpu_usage = process_info.cpu_usage;
                stats.memory_usage = process_info.memory_usage;
                stats.restart_count = process_info.restart_count;
            }
        }
        
        stats.timestamp = timestamp;
        result.push_back(stats);
    }
    
    return result;
}

StreamPerformanceStats StatisticsManager::GetStreamStats(const std::string& app, const std::string& stream) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    StreamPerformanceStats stats;
    
    if (!stream_manager_) {
        return stats;
    }
    
    auto metadata = stream_manager_->GetStreamMetadata(app, stream);
    if (metadata.status == streaming::StreamStatus::Stopped && metadata.app.empty()) {
        // 流不存在
        return stats;
    }
    
    auto now = std::chrono::system_clock::now();
    int64_t timestamp = std::chrono::duration_cast<std::chrono::seconds>(
        now.time_since_epoch()).count();
    
    stats.app = metadata.app;
    stats.stream = metadata.stream;
    stats.protocol = metadata.protocol;
    stats.bytes_speed = metadata.bytes_speed;
    stats.total_bytes = metadata.total_bytes;
    stats.reader_count = metadata.reader_count;
    
    // 状态文本
    switch (metadata.status) {
        case streaming::StreamStatus::Stopped:
            stats.status = "stopped";
            break;
        case streaming::StreamStatus::Starting:
            stats.status = "starting";
            break;
        case streaming::StreamStatus::Running:
            stats.status = "running";
            break;
        case streaming::StreamStatus::Stopping:
            stats.status = "stopping";
            break;
        case streaming::StreamStatus::Error:
            stats.status = "error";
            break;
    }
    
    // 计算运行时间
    if (metadata.create_time > 0) {
        stats.uptime = timestamp - metadata.create_time;
    }
    
    // 从 ProcessManager 获取进程资源使用（如果有 PID）
    if (process_manager_ && metadata.pid > 0) {
        auto process_info = process_manager_->GetProcessInfo(app + "/" + stream);
        if (process_info.status != process::ProcessStatus::Stopped || process_info.pid > 0) {
            stats.cpu_usage = process_info.cpu_usage;
            stats.memory_usage = process_info.memory_usage;
            stats.restart_count = process_info.restart_count;
        }
    }
    
    stats.timestamp = timestamp;
    return stats;
}

std::vector<ProtocolStatistics> StatisticsManager::GetProtocolStatistics() {
    std::lock_guard<std::mutex> lock(mutex_);
    
    std::map<std::string, ProtocolStatistics> protocol_map;
    
    if (!stream_manager_) {
        return std::vector<ProtocolStatistics>();
    }
    
    auto all_streams = stream_manager_->GetAllStreams();
    
    for (const auto& stream : all_streams) {
        std::string protocol = stream.protocol;
        if (protocol.empty()) {
            protocol = "unknown";
        }
        
        if (protocol_map.find(protocol) == protocol_map.end()) {
            ProtocolStatistics stats;
            stats.protocol = protocol;
            protocol_map[protocol] = stats;
        }
        
        auto& stats = protocol_map[protocol];
        stats.stream_count++;
        
        if (stream.status == streaming::StreamStatus::Running) {
            stats.running_count++;
            stats.total_bytes += stream.total_bytes;
            stats.bytes_speed += stream.bytes_speed;
            stats.total_viewers += stream.reader_count;
        }
    }
    
    std::vector<ProtocolStatistics> result;
    for (auto& pair : protocol_map) {
        result.push_back(pair.second);
    }
    
    return result;
}

std::map<std::string, int> StatisticsManager::GetErrorStatistics() {
    std::lock_guard<std::mutex> lock(mutex_);
    
    std::map<std::string, int> error_stats;
    error_stats["total"] = 0;
    error_stats["network"] = 0;
    error_stats["protocol"] = 0;
    error_stats["config"] = 0;
    error_stats["auth"] = 0;
    error_stats["unknown"] = 0;
    
    if (!process_manager_) {
        return error_stats;
    }
    
    auto all_processes = process_manager_->GetAllProcesses();
    
    for (const auto& process : all_processes) {
        if (process.status == process::ProcessStatus::Error) {
            error_stats["total"]++;
            
            switch (process.last_error) {
                case process::ProcessErrorType::NetworkError:
                    error_stats["network"]++;
                    break;
                case process::ProcessErrorType::ProtocolError:
                    error_stats["protocol"]++;
                    break;
                case process::ProcessErrorType::ConfigError:
                    error_stats["config"]++;
                    break;
                case process::ProcessErrorType::AuthError:
                    error_stats["auth"]++;
                    break;
                default:
                    error_stats["unknown"]++;
                    break;
            }
        }
    }
    
    // 统计流错误
    if (stream_manager_) {
        auto all_streams = stream_manager_->GetAllStreams();
        for (const auto& stream : all_streams) {
            if (stream.status == streaming::StreamStatus::Error) {
                error_stats["total"]++;
            }
        }
    }
    
    return error_stats;
}

} // namespace monitoring

