#ifndef PROCESS_PROCESS_MANAGER_HPP
#define PROCESS_PROCESS_MANAGER_HPP

#include <string>
#include <vector>
#include <map>
#include <memory>
#include <mutex>
#include <thread>
#include <atomic>
#include <chrono>
#include <functional>

namespace process {

/**
 * @brief 进程错误类型
 */
enum class ProcessErrorType {
    None,               // 无错误
    NetworkError,       // 网络错误（可重启）
    ProtocolError,      // 协议错误（可重启）
    ConfigError,        // 配置错误（不可重启）
    AuthError,          // 认证错误（不可重启）
    UnknownError        // 未知错误（可重启）
};

/**
 * @brief Phase 2.3: 进程监控分层策略
 * 
 * 根据进程状态和运行情况，动态调整监控频率：
 * - Critical: 1秒 - Error状态、新启动、高CPU使用
 * - Normal: 5秒 - 常规Running状态
 * - Low: 30秒 - Stopped状态
 * - Minimal: 60秒 - 长期稳定的Running状态
 */
enum class MonitorTier {
    Critical = 0,  // 1s - 需要密切监控
    Normal = 1,    // 5s - 标准监控
    Low = 2,       // 30s - 低频监控
    Minimal = 3    // 最低监控
};


/**
 * @brief 进程状态
 */
enum class ProcessStatus {
    Stopped,    // 已停止
    Starting,   // 启动中
    Running,    // 运行中
    Stopping,   // 停止中
    Error,      // 错误
    Restarting  // 重启中
};

/**
 * @brief 进程信息
 */
struct ProcessInfo {
    std::string process_id;              // 进程 ID（唯一标识）
    std::string name;                    // 进程名称
    pid_t pid = 0;                       // 系统 PID
    ProcessStatus status = ProcessStatus::Stopped;
    ProcessErrorType last_error = ProcessErrorType::None;
    
    std::string command;                 // FFmpeg 命令
    std::string source_url;              // 源流地址
    std::string target_app;              // 目标应用名
    std::string target_stream;           // 目标流名
    
    int restart_count = 0;               // 重启次数
    int max_restarts = 3;                // 最大重启次数
    std::chrono::system_clock::time_point last_restart_time;  // 最后重启时间
    
    // 资源限制
    int cpu_limit = 0;                   // CPU 限制（百分比，0 表示无限制）
    int64_t memory_limit = 0;            // 内存限制（字节，0 表示无限制）
    
    // 资源监控
    double cpu_usage = 0.0;              // CPU 使用率（百分比）
    int64_t memory_usage = 0;            // 内存使用（字节）
    
    // 时间戳
    std::chrono::system_clock::time_point start_time;
    std::chrono::system_clock::time_point last_update_time;
    
    // Phase 2.3: 分层监控
    MonitorTier tier = MonitorTier::Normal;  // 监控层级
    std::chrono::system_clock::time_point last_monitor_time;  // 上次监控时间
    
    // 日志文件路径
    std::string log_file;
};

/**
 * @brief 进程统计信息（避免完全复制）
 */
struct ProcessStatistics {
    int total_processes = 0;
    int running_processes = 0;
    int error_processes = 0;
    int restarting_processes = 0;
    
    // 错误类型统计
    int network_errors = 0;
    int protocol_errors = 0;
    int config_errors = 0;
    int auth_errors = 0;
    int unknown_errors = 0;
};

/**
 * @brief 进程监控回调函数类型
 */
using ProcessMonitorCallback = std::function<void(const ProcessInfo& info, ProcessErrorType error_type)>;

/**
 * @brief FFmpeg 进程管理器
 * 
 * 功能：
 * 1. 统一的进程管理（启动、停止、监控）
 * 2. 进程监控（检测异常退出、监控输出、资源监控）
 * 3. 自动重启机制（错误分类、重启策略、指数退避）
 * 4. 资源限制（CPU、内存限制）
 */
class ProcessManager {
public:
    /**
     * @brief 构造函数
     * @param max_restarts 最大重启次数（默认 3）
     * @param monitor_interval 监控间隔（秒，默认 5）
     * @param max_processes 最大进程数（0 表示无限制，默认 0）
     */
    ProcessManager(int max_restarts = 3, int monitor_interval = 5, int max_processes = 0);

    /**
     * @brief 析构函数
     */
    ~ProcessManager();

    /**
     * @brief 启动进程
     * @param process_id 进程 ID（唯一标识）
     * @param name 进程名称
     * @param command FFmpeg 命令
     * @param source_url 源流地址
     * @param target_app 目标应用名
     * @param target_stream 目标流名
     * @param max_restarts 最大重启次数（默认 3）
     * @param cpu_limit CPU 限制（百分比，0 表示无限制）
     * @param memory_limit 内存限制（字节，0 表示无限制）
     * @return 是否成功
     */
    bool StartProcess(const std::string& process_id,
                     const std::string& name,
                     const std::string& command,
                     const std::string& source_url,
                     const std::string& target_app,
                     const std::string& target_stream,
                     int max_restarts = 3,
                     int cpu_limit = 0,
                     int64_t memory_limit = 0);

    /**
     * @brief 停止进程
     * @param process_id 进程 ID
     * @return 是否成功
     */
    bool StopProcess(const std::string& process_id);

    /**
     * @brief 停止所有进程
     */
    void StopAllProcesses();

    /**
     * @brief 获取进程信息
     * @param process_id 进程 ID
     * @return 进程信息，不存在返回空对象（status == Stopped）
     */
    ProcessInfo GetProcessInfo(const std::string& process_id) const;

    /**
     * @brief 获取所有进程信息
     * @return 进程信息列表
     */
    std::vector<ProcessInfo> GetAllProcesses() const;

    /**
     * @brief 获取进程统计信息（线程安全，避免复制）
     * @return 统计信息结构体
     */
    ProcessStatistics GetProcessStatistics() const;

    /**
     * @brief 检查进程是否运行中
     * @param process_id 进程 ID
     * @return 是否运行中
     */
    bool IsProcessRunning(const std::string& process_id) const;

    /**
     * @brief 设置进程监控回调
     * @param callback 回调函数
     */
    void SetMonitorCallback(ProcessMonitorCallback callback);

    /**
     * @brief 设置最大并发进程数
     * @param max_processes 最大进程数（0 表示无限制）
     */
    void SetMaxProcesses(int max_processes);

private:
    /**
     * @brief 启动 FFmpeg 进程（内部实现）
     * @param info 进程信息
     * @return 是否成功
     */
    bool StartFFmpegProcess(ProcessInfo& info);

    /**
     * @brief 停止 FFmpeg 进程（内部实现）
     * @param info 进程信息
     * @return 是否成功
     */
    bool StopFFmpegProcess(ProcessInfo& info);


    /**
     * @brief 监控线程函数
     */
    void MonitorThread();

    /**
     * @brief 监控单个进程
     * @param process_id 进程 ID
     */
    void MonitorProcess(const std::string& process_id);

    /**
     * @brief 分析进程错误（从日志或退出码）
     * @param info 进程信息
     * @return 错误类型
     */
    ProcessErrorType AnalyzeError(const ProcessInfo& info);

    /**
     * @brief 尝试重启进程
     * @param process_id 进程 ID
     * @return 是否成功
     */
    bool TryRestartProcess(const std::string& process_id);

    /**
     * @brief 更新进程资源使用情况
     * @param info 进程信息
     */
    void UpdateProcessResources(ProcessInfo& info);

    /**
     * @brief 应用资源限制
     * @param pid 进程 PID
     * @param cpu_limit CPU 限制
     * @param memory_limit 内存限制
     */
    void ApplyResourceLimits(pid_t pid, int cpu_limit, int64_t memory_limit);

    // ============================================
    // Phase 2.3: 分层监控辅助方法
    // ============================================
    
    /**
     * @brief 获取监控层级对应的间隔时间（秒）
     * @param tier 监控层级
     * @return 间隔秒数
     */
    int GetTierInterval(MonitorTier tier) const;
    
    /**
     * @brief 更新进程的监控层级
     * @param process_id 进程ID
     * 
     * 根据进程状态、运行时长、资源使用等自动调整tier
     * 调用前必须已持有mutex_锁
     */
    void UpdateProcessTier(const std::string& process_id);

    mutable std::mutex mutex_;
    std::map<std::string, ProcessInfo> processes_;
    
    std::atomic<bool> running_;
    std::thread monitor_thread_;
    
    ProcessMonitorCallback monitor_callback_;
    int max_processes_ = 0;  // 0 表示无限制
    int default_max_restarts_ = 3;  // 默认最大重启次数
    int monitor_interval_seconds_ = 5;  // 监控间隔（秒）
    
    // 监控间隔（秒）
    static constexpr int MONITOR_INTERVAL_SECONDS = 5;
};

} // namespace process

#endif // PROCESS_PROCESS_MANAGER_HPP

