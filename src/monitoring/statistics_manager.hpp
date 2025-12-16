#ifndef MONITORING_STATISTICS_MANAGER_HPP
#define MONITORING_STATISTICS_MANAGER_HPP

#include <string>
#include <vector>
#include <map>
#include <memory>
#include <mutex>
#include <chrono>
#include <cstdint>

namespace process {
class ProcessManager;
}

namespace streaming {
class StreamManager;
}

namespace monitoring {

/**
 * @brief 系统统计信息
 */
struct SystemStatistics {
    // 流统计
    int total_streams = 0;              // 总流数
    int running_streams = 0;            // 运行中的流数
    int stopped_streams = 0;            // 已停止的流数
    int error_streams = 0;              // 错误流数
    int starting_streams = 0;           // 启动中的流数
    
    // 进程统计
    int total_processes = 0;            // 总进程数
    int running_processes = 0;          // 运行中的进程数
    int error_processes = 0;            // 错误进程数
    int restarting_processes = 0;       // 重启中的进程数
    
    // 资源统计
    double total_cpu_usage = 0.0;       // 总 CPU 使用率（%）
    int64_t total_memory_usage = 0;     // 总内存使用（字节）
    int64_t total_network_speed = 0;    // 总网络速度（字节/秒）
    int64_t total_bytes = 0;            // 总传输字节数
    
    // 观看者统计
    int total_viewers = 0;              // 总观看者数
    
    // 错误统计
    int total_errors = 0;               // 总错误数
    int network_errors = 0;             // 网络错误数
    int protocol_errors = 0;            // 协议错误数
    int config_errors = 0;              // 配置错误数
    int auth_errors = 0;                // 认证错误数
    
    // 时间戳
    int64_t timestamp = 0;              // 统计时间戳（Unix 时间戳，秒）
};

/**
 * @brief 流性能统计
 */
struct StreamPerformanceStats {
    std::string app;                    // 应用名
    std::string stream;                 // 流名
    std::string protocol;               // 协议类型
    
    // 资源使用
    double cpu_usage = 0.0;             // CPU 使用率（%）
    int64_t memory_usage = 0;           // 内存使用（字节）
    
    // 网络统计
    int64_t bytes_speed = 0;            // 字节速度（字节/秒）
    int64_t total_bytes = 0;            // 总字节数
    int reader_count = 0;               // 观看者数量
    
    // 状态信息
    std::string status;                 // 流状态
    int64_t uptime = 0;                 // 运行时间（秒）
    int restart_count = 0;              // 重启次数
    
    // 时间戳
    int64_t timestamp = 0;              // 统计时间戳
};

/**
 * @brief 协议统计
 */
struct ProtocolStatistics {
    std::string protocol;               // 协议名称
    int stream_count = 0;               // 流数量
    int running_count = 0;              // 运行中的流数量
    int64_t total_bytes = 0;            // 总字节数
    int64_t bytes_speed = 0;            // 总字节速度
    int total_viewers = 0;              // 总观看者数
};

/**
 * @brief 统计管理器
 * 
 * 功能：
 * 1. 收集系统统计信息
 * 2. 收集流性能统计
 * 3. 收集协议统计
 * 4. 提供统计查询接口
 */
class StatisticsManager {
public:
    /**
     * @brief 构造函数
     * @param stream_manager 流管理器
     * @param process_manager 进程管理器（可选）
     */
    StatisticsManager(std::shared_ptr<streaming::StreamManager> stream_manager,
                      std::shared_ptr<process::ProcessManager> process_manager = nullptr);

    /**
     * @brief 获取系统统计信息
     * @return 系统统计信息
     */
    SystemStatistics GetSystemStatistics();

    /**
     * @brief 获取所有流的性能统计
     * @return 流性能统计列表
     */
    std::vector<StreamPerformanceStats> GetAllStreamStats();

    /**
     * @brief 获取指定流的性能统计
     * @param app 应用名
     * @param stream 流名
     * @return 流性能统计（如果不存在返回空对象）
     */
    StreamPerformanceStats GetStreamStats(const std::string& app, const std::string& stream);

    /**
     * @brief 按协议获取统计信息
     * @return 协议统计列表
     */
    std::vector<ProtocolStatistics> GetProtocolStatistics();

    /**
     * @brief 获取错误统计
     * @return 错误统计映射（错误类型 -> 数量）
     */
    std::map<std::string, int> GetErrorStatistics();

private:
    std::shared_ptr<streaming::StreamManager> stream_manager_;
    std::shared_ptr<process::ProcessManager> process_manager_;
    mutable std::mutex mutex_;
};

} // namespace monitoring

#endif // MONITORING_STATISTICS_MANAGER_HPP

