#include "process/process_manager.hpp"
#include "process/process_monitor.hpp"
#include "utils/logger.hpp"
#include <unistd.h>
#include <sys/wait.h>
#include <signal.h>
#include <fcntl.h>
#include <errno.h>
#include <cstring>
#include <sstream>
#include <fstream>
#include <algorithm>
#include <sys/resource.h>
#include <sys/time.h>
#include <thread>
#include <chrono>

#ifdef __APPLE__
#include <libproc.h>
#else
#include <sys/sysinfo.h>
#endif

namespace process {

ProcessManager::ProcessManager(int max_restarts, int monitor_interval, int max_processes) 
    : running_(true), max_processes_(max_processes) {
    // 设置默认最大重启次数（如果进程未指定）
    default_max_restarts_ = max_restarts;
    monitor_interval_seconds_ = monitor_interval;
    
    monitor_thread_ = std::thread(&ProcessManager::MonitorThread, this);
    LOG_INFO("Process Manager 初始化完成 (最大重启次数: {}, 监控间隔: {}s, 最大进程数: {})", 
             max_restarts, monitor_interval, max_processes > 0 ? std::to_string(max_processes) : "无限制");
}

ProcessManager::~ProcessManager() {
    running_ = false;
    StopAllProcesses();
    
    if (monitor_thread_.joinable()) {
        monitor_thread_.join();
    }
    
    LOG_INFO("Process Manager 析构完成");
}

bool ProcessManager::StartProcess(const std::string& process_id,
                                  const std::string& name,
                                  const std::string& command,
                                  const std::string& source_url,
                                  const std::string& target_app,
                                  const std::string& target_stream,
                                  int max_restarts,
                                  int cpu_limit,
                                  int64_t memory_limit) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    // 检查最大进程数限制
    if (max_processes_ > 0 && static_cast<int>(processes_.size()) >= max_processes_) {
        LOG_ERROR("达到最大进程数限制: {}", max_processes_);
        return false;
    }
    
    // 检查进程是否已存在
    auto it = processes_.find(process_id);
    if (it != processes_.end()) {
        if (it->second.status == ProcessStatus::Running || 
            it->second.status == ProcessStatus::Starting) {
            LOG_WARN("进程已存在且运行中: {}", process_id);
            return true;
        }
        // 如果进程存在但已停止，先停止旧进程
        StopFFmpegProcess(it->second);
    }
    
    // 创建进程信息
    ProcessInfo info;
    info.process_id = process_id;
    info.name = name;
    info.command = command;
    info.source_url = source_url;
    info.target_app = target_app;
    info.target_stream = target_stream;
    info.max_restarts = max_restarts;
    info.restart_count = 0;
    info.cpu_limit = cpu_limit;
    info.memory_limit = memory_limit;
    info.status = ProcessStatus::Starting;
    info.start_time = std::chrono::system_clock::now();
    info.last_update_time = info.start_time;
    
    // Phase 2.3: 初始化分层监控
    info.tier = MonitorTier::Critical;  // Starting状态使用Critical tier
    info.last_monitor_time = info.start_time;
    
    // 生成日志文件路径
    std::ostringstream log_oss;
    log_oss << "/tmp/ffmpeg_" << target_app << "_" << target_stream << ".log";
    info.log_file = log_oss.str();
    
    // 启动进程
    if (!StartFFmpegProcess(info)) {
        LOG_ERROR("启动进程失败: {}", process_id);
        return false;
    }
    
    // 保存进程信息
    processes_[process_id] = info;
    
    LOG_INFO("进程启动成功: {} (PID: {})", process_id, info.pid);
    return true;
}

bool ProcessManager::StopProcess(const std::string& process_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    auto it = processes_.find(process_id);
    if (it == processes_.end()) {
        LOG_WARN("进程不存在: {}", process_id);
        return false;
    }
    
    // 禁用自动重启：将 max_restarts 设为 0，防止停止后自动重启
    it->second.max_restarts = 0;
    it->second.restart_count = 999;  // 设置为一个很大的值，确保不会触发重启
    
    bool success = StopFFmpegProcess(it->second);
    processes_.erase(it);
    
    if (success) {
        LOG_INFO("进程停止成功: {}", process_id);
    } else {
        LOG_ERROR("进程停止失败: {}", process_id);
    }
    
    return success;
}

void ProcessManager::StopAllProcesses() {
    std::lock_guard<std::mutex> lock(mutex_);
    
    for (auto& pair : processes_) {
        StopFFmpegProcess(pair.second);
    }
    
    processes_.clear();
    LOG_INFO("所有进程已停止");
}

ProcessInfo ProcessManager::GetProcessInfo(const std::string& process_id) const {
    std::lock_guard<std::mutex> lock(mutex_);
    
    auto it = processes_.find(process_id);
    if (it == processes_.end()) {
        return ProcessInfo();  // 返回空对象
    }
    
    return it->second;
}

std::vector<ProcessInfo> ProcessManager::GetAllProcesses() const {
    std::lock_guard<std::mutex> lock(mutex_);
    
    std::vector<ProcessInfo> result;
    result.reserve(processes_.size());
    
    for (const auto& pair : processes_) {
        result.push_back(pair.second);
    }
    
    return result;
}

ProcessStatistics ProcessManager::GetProcessStatistics() const {
    std::lock_guard<std::mutex> lock(mutex_);
    
    ProcessStatistics stats;
    stats.total_processes = static_cast<int>(processes_.size());
    
    for (const auto& pair : processes_) {
        const auto& process = pair.second;
        switch (process.status) {
            case ProcessStatus::Running:
                stats.running_processes++;
                break;
            case ProcessStatus::Error:
                stats.error_processes++;
                switch (process.last_error) {
                    case ProcessErrorType::NetworkError:
                        stats.network_errors++;
                        break;
                    case ProcessErrorType::ProtocolError:
                        stats.protocol_errors++;
                        break;
                    case ProcessErrorType::ConfigError:
                        stats.config_errors++;
                        break;
                    case ProcessErrorType::AuthError:
                        stats.auth_errors++;
                        break;
                    default:
                        stats.unknown_errors++;
                        break;
                }
                break;
            case ProcessStatus::Restarting:
                stats.restarting_processes++;
                break;
            default:
                break;
        }
    }
    
    return stats;
}

bool ProcessManager::IsProcessRunning(const std::string& process_id) const {
    std::lock_guard<std::mutex> lock(mutex_);
    
    auto it = processes_.find(process_id);
    if (it == processes_.end()) {
        return false;
    }
    
    return it->second.status == ProcessStatus::Running;
}

void ProcessManager::SetMonitorCallback(ProcessMonitorCallback callback) {
    std::lock_guard<std::mutex> lock(mutex_);
    monitor_callback_ = callback;
}

void ProcessManager::SetMaxProcesses(int max_processes) {
    std::lock_guard<std::mutex> lock(mutex_);
    max_processes_ = max_processes;
}

bool ProcessManager::StartFFmpegProcess(ProcessInfo& info) {
    LOG_DEBUG("启动 FFmpeg 进程: {}", info.command);
    
    // 使用 fork + exec 启动 FFmpeg 进程
    pid_t pid = fork();
    if (pid < 0) {
        LOG_ERROR("fork 失败: {}", strerror(errno));
        return false;
    } else if (pid == 0) {
        // 子进程：执行 FFmpeg 命令
        // 重定向输出到日志文件
        int log_fd = open(info.log_file.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
        if (log_fd >= 0) {
            dup2(log_fd, STDOUT_FILENO);
            dup2(log_fd, STDERR_FILENO);
            close(log_fd);
        } else {
            // 如果无法打开日志文件，重定向到 /dev/null
            int null_fd = open("/dev/null", O_WRONLY);
            if (null_fd >= 0) {
                dup2(null_fd, STDOUT_FILENO);
                dup2(null_fd, STDERR_FILENO);
                close(null_fd);
            }
        }
        
        // 重定向 stdin 到 /dev/null 防止读取 stdin 导致 SIGTTIN 暂停
        int null_in_fd = open("/dev/null", O_RDONLY);
        if (null_in_fd >= 0) {
            dup2(null_in_fd, STDIN_FILENO);
            close(null_in_fd);
        }
        
        // 应用资源限制（在子进程中）
        if (info.cpu_limit > 0 || info.memory_limit > 0) {
            ApplyResourceLimits(getpid(), info.cpu_limit, info.memory_limit);
        }
        
        // 执行命令
        // Critical Fix: Use 'exec' to replace the shell process with the command process.
        // This ensures that the PID we track is the actual FFmpeg process, not the shell wrapper.
        // When we send SIGTERM/SIGKILL, it goes directly to FFmpeg, preventing zombie processes.
        std::string exec_command = "exec " + info.command;
        execl("/bin/sh", "sh", "-c", exec_command.c_str(), nullptr);
        exit(1);  // 如果 exec 失败
    } else {
        // 父进程：保存 PID
        info.pid = pid;
        info.status = ProcessStatus::Starting;
        info.last_update_time = std::chrono::system_clock::now();
        
        // 等待一小段时间，检查进程是否成功启动
        usleep(1000000);  // 1秒
        
        if (!ProcessMonitor::IsProcessAlive(pid)) {
            LOG_ERROR("FFmpeg 进程启动失败 (PID: {})", pid);
            info.status = ProcessStatus::Error;
            return false;
        }
        
        // 进程在运行，状态设为 Running
        info.status = ProcessStatus::Running;
        info.last_update_time = std::chrono::system_clock::now();
        
        return true;
    }
}

bool ProcessManager::StopFFmpegProcess(ProcessInfo& info) {
    if (info.pid <= 0) {
        return true;
    }
    
    info.status = ProcessStatus::Stopping;
    info.last_update_time = std::chrono::system_clock::now();
    
    // 发送 SIGTERM 信号
    if (kill(info.pid, SIGTERM) != 0) {
        LOG_WARN("发送 SIGTERM 失败 (PID: {}): {}", info.pid, strerror(errno));
        // 如果进程不存在，认为停止成功
        if (errno == ESRCH) {
            info.pid = 0;
            info.status = ProcessStatus::Stopped;
            return true;
        }
        return false;
    }
    
    // 等待进程退出（最多等待 5 秒）
    for (int i = 0; i < 50; ++i) {
        if (!ProcessMonitor::IsProcessAlive(info.pid)) {
            info.pid = 0;
            info.status = ProcessStatus::Stopped;
            return true;
        }
        usleep(100000);  // 100ms
    }
    
    // 如果进程还在运行，发送 SIGKILL
    LOG_WARN("进程未响应 SIGTERM，发送 SIGKILL (PID: {})", info.pid);
    if (kill(info.pid, SIGKILL) == 0) {
        // SIGKILL 后等待进程退出（最多等待 2 秒，每次 100ms）
        for (int i = 0; i < 20; ++i) {
            usleep(100000);  // 等待 100ms
            if (!ProcessMonitor::IsProcessAlive(info.pid)) {
                info.pid = 0;
                info.status = ProcessStatus::Stopped;
                return true;
            }
        }
        // 如果 2 秒后进程仍然存在，记录警告但认为停止成功（可能是 zombie 进程）
        LOG_WARN("SIGKILL 后进程仍存在 (PID: {})，可能是 zombie 进程，认为停止成功", info.pid);
        info.pid = 0;
        info.status = ProcessStatus::Stopped;
        return true;
    } else {
        // SIGKILL 发送失败，检查进程是否已经退出
        if (errno == ESRCH || !ProcessMonitor::IsProcessAlive(info.pid)) {
            info.pid = 0;
            info.status = ProcessStatus::Stopped;
            return true;
        }
    }
    
    info.status = ProcessStatus::Error;
    return false;
}


void ProcessManager::MonitorThread() {
    LOG_INFO("Phase 2.3: 进程监控线程启动（分层监控模式）");
    
    while (running_) {
        // Phase 2.3: 最小等待间隔为1秒（Critical tier）
        std::this_thread::sleep_for(std::chrono::seconds(1));
        
        if (!running_) {
            break;
        }
        
        auto now = std::chrono::system_clock::now();
        
        // 获取本周期需要监控的进程
        std::vector<std::pair<std::string, MonitorTier>> to_monitor;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            for (const auto& [id, info] : processes_) {
                // 根据tier计算间隔
                int interval = GetTierInterval(info.tier);
                
                // 计算距离上次监控的时间
                auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(
                    now - info.last_monitor_time).count();
                
                // 如果到达监控间隔，加入监控列表
                if (elapsed >= interval) {
                    to_monitor.push_back({id, info.tier});
                }
            }
        }
        
        // 在锁外监控选中的进程
        for (const auto& [id, tier] : to_monitor) {
            MonitorProcess(id);
            
            // 更新last_monitor_time
            {
                std::lock_guard<std::mutex> lock(mutex_);
                auto it = processes_.find(id);
                if (it != processes_.end()) {
                    it->second.last_monitor_time = now;
                }
            }
        }
    }
    
    LOG_INFO("进程监控线程退出");
}

void ProcessManager::MonitorProcess(const std::string& process_id) {
    // 第一步：在锁外执行所有系统调用，避免阻塞其他读操作
    pid_t pid = 0;
    ProcessStatus current_status = ProcessStatus::Stopped;
    int max_restarts = 0;
    int restart_count = 0;
    std::string log_file;
    
    {
        // 快速读取进程信息（最小锁时间）
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = processes_.find(process_id);
    if (it == processes_.end()) {
        return;
    }
        
        const ProcessInfo& info = it->second;
        pid = info.pid;
        current_status = info.status;
        max_restarts = info.max_restarts;
        restart_count = info.restart_count;
        log_file = info.log_file;
    }
    
    // 第二步：在锁外执行系统调用（这些操作可能很慢）
    bool is_alive = (pid > 0) ? ProcessMonitor::IsProcessAlive(pid) : false;
    ProcessResources resources;
    if (pid > 0) {
        resources = ProcessMonitor::GetProcessResources(pid);
    }
    
    // 第三步：检查进程状态并分析错误（在锁外执行文件 I/O）
    ProcessErrorType error_type = ProcessErrorType::UnknownError;
    bool needs_restart = false;
    bool should_restart = false;
    
    if ((current_status == ProcessStatus::Running || current_status == ProcessStatus::Starting) && !is_alive) {
        // 进程异常退出，分析错误（在锁外读取日志文件）
        ProcessInfo temp_info;
        temp_info.pid = pid;
        temp_info.log_file = log_file;
        error_type = AnalyzeError(temp_info);
        
        // 判断是否需要重启
        needs_restart = true;
        if (restart_count < max_restarts) {
            if (error_type == ProcessErrorType::NetworkError ||
                error_type == ProcessErrorType::ProtocolError ||
                error_type == ProcessErrorType::UnknownError) {
                should_restart = true;
            }
        }
    }
    
    // 第四步：更新进程信息（加锁更新）
    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = processes_.find(process_id);
        if (it == processes_.end()) {
            return;  // 进程可能已被删除
        }
    
    ProcessInfo& info = it->second;
    
    // 更新资源使用情况
        if (pid > 0 && pid == info.pid) {
            info.cpu_usage = resources.cpu_usage;
            info.memory_usage = resources.memory_usage;
        }
        
        // 更新进程状态
        if (needs_restart) {
            LOG_WARN("进程异常退出: {} (PID: {})", process_id, pid);
            info.status = ProcessStatus::Error;
            info.last_error = error_type;
            
            if (should_restart) {
                // 尝试重启（TryRestartProcess 内部会处理锁）
                // 注意：TryRestartProcess 需要锁，但我们已经持有锁，需要先释放
                // 为了避免死锁，我们在锁外调用 TryRestartProcess
                // 但 TryRestartProcess 内部会再次加锁，这是安全的
            } else {
                if (restart_count >= max_restarts) {
                    LOG_ERROR("进程超过最大重启次数: {} (已重启 {} 次)", process_id, restart_count);
                } else {
                    LOG_ERROR("进程错误不可重启: {} (错误类型: {})", process_id, 
                             static_cast<int>(error_type));
                }
                if (monitor_callback_) {
                    monitor_callback_(info, error_type);
            }
        }
    }
    
    info.last_update_time = std::chrono::system_clock::now();
    
    // Phase 2.3: 根据状态变化更新tier
    UpdateProcessTier(process_id);
    }
    
    // 第五步：在锁外执行重启操作（避免长时间持有锁）
    if (needs_restart && should_restart) {
        TryRestartProcess(process_id);
    }
}

ProcessErrorType ProcessManager::AnalyzeError(const ProcessInfo& info) {
    // 读取日志文件，分析错误类型
    std::ifstream log_file(info.log_file);
    if (!log_file.is_open()) {
        return ProcessErrorType::UnknownError;
    }
    
    std::string line;
    std::string last_lines;
    int line_count = 0;
    
    // 读取最后 50 行
    while (std::getline(log_file, line)) {
        last_lines += line + "\n";
        line_count++;
        if (line_count > 50) {
            // 只保留最后 50 行
            size_t pos = last_lines.find('\n');
            if (pos != std::string::npos) {
                last_lines = last_lines.substr(pos + 1);
            }
        }
    }
    
    // 分析错误类型
    std::string lower_log = last_lines;
    std::transform(lower_log.begin(), lower_log.end(), lower_log.begin(), ::tolower);
    
    // 网络错误（可重启）
    if (lower_log.find("connection refused") != std::string::npos ||
        lower_log.find("connection timed out") != std::string::npos ||
        lower_log.find("timeout") != std::string::npos ||
        lower_log.find("network is unreachable") != std::string::npos ||
        lower_log.find("no route to host") != std::string::npos ||
        lower_log.find("connection reset") != std::string::npos ||
        lower_log.find("connection closed") != std::string::npos ||
        lower_log.find("broken pipe") != std::string::npos ||
        lower_log.find("end of file") != std::string::npos ||
        lower_log.find("server returned 404") != std::string::npos ||
        lower_log.find("server returned 500") != std::string::npos ||
        lower_log.find("server returned 502") != std::string::npos ||
        lower_log.find("server returned 503") != std::string::npos) {
        return ProcessErrorType::NetworkError;
    }
    
    // 认证错误（不可重启）
    if (lower_log.find("unauthorized") != std::string::npos ||
        lower_log.find("authentication failed") != std::string::npos ||
        lower_log.find("401") != std::string::npos ||
        lower_log.find("403") != std::string::npos ||
        lower_log.find("forbidden") != std::string::npos ||
        lower_log.find("access denied") != std::string::npos ||
        lower_log.find("invalid credentials") != std::string::npos) {
        return ProcessErrorType::AuthError;
    }
    
    // 配置错误（不可重启）
    if (lower_log.find("invalid argument") != std::string::npos ||
        lower_log.find("no such file") != std::string::npos ||
        lower_log.find("permission denied") != std::string::npos ||
        lower_log.find("invalid data") != std::string::npos ||
        lower_log.find("invalid url") != std::string::npos ||
        lower_log.find("no such device") != std::string::npos ||
        lower_log.find("operation not permitted") != std::string::npos ||
        lower_log.find("bad file descriptor") != std::string::npos) {
        return ProcessErrorType::ConfigError;
    }
    
    // 协议错误（可重启）
    if (lower_log.find("protocol not found") != std::string::npos ||
        lower_log.find("unsupported codec") != std::string::npos ||
        lower_log.find("format not found") != std::string::npos ||
        lower_log.find("codec not found") != std::string::npos ||
        lower_log.find("stream not found") != std::string::npos ||
        lower_log.find("invalid data found") != std::string::npos ||
        lower_log.find("error while decoding") != std::string::npos) {
        return ProcessErrorType::ProtocolError;
    }
    
    return ProcessErrorType::UnknownError;
}

bool ProcessManager::TryRestartProcess(const std::string& process_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    auto it = processes_.find(process_id);
    if (it == processes_.end()) {
        return false;
    }
    
    ProcessInfo& info = it->second;
    
    // 检查是否超过最大重启次数
    if (info.restart_count >= info.max_restarts) {
        LOG_ERROR("进程超过最大重启次数: {} (已重启 {} 次，最大 {} 次)", 
                 process_id, info.restart_count, info.max_restarts);
        info.status = ProcessStatus::Error;
        if (monitor_callback_) {
            monitor_callback_(info, info.last_error);
        }
        return false;
    }
    
    // 计算重启间隔（指数退避策略）
    auto now = std::chrono::system_clock::now();
    int backoff_seconds = 1 << info.restart_count;  // 1s, 2s, 4s, 8s...
    if (backoff_seconds > 8) {
        backoff_seconds = 8;  // 最大 8 秒
    }
    
    // 如果是第一次重启，last_restart_time 可能未初始化
    if (info.last_restart_time.time_since_epoch().count() == 0) {
        info.last_restart_time = now;
    }
    
    auto time_since_last_restart = std::chrono::duration_cast<std::chrono::seconds>(
        now - info.last_restart_time).count();
    
    if (time_since_last_restart < backoff_seconds) {
        // 还没到重启时间，等待指数退避
        LOG_DEBUG("进程 {} 等待重启（还需等待 {} 秒）", process_id, 
                 backoff_seconds - time_since_last_restart);
        return false;
    }
    
    LOG_INFO("尝试重启进程: {} (第 {} 次重启，错误类型: {})", 
             process_id, info.restart_count + 1, static_cast<int>(info.last_error));
    
    info.status = ProcessStatus::Restarting;
    info.restart_count++;
    info.last_restart_time = now;
    
    // Phase 2.3: 重启时设置为Critical tier
    info.tier = MonitorTier::Critical;
    
    // 停止旧进程（如果还在运行）
    if (info.pid > 0) {
        StopFFmpegProcess(info);
        // 等待一小段时间确保进程完全退出
        usleep(500000);  // 500ms
    }
    
    // 启动新进程
    if (StartFFmpegProcess(info)) {
        LOG_INFO("进程重启成功: {} (PID: {})", process_id, info.pid);
        return true;
    } else {
        LOG_ERROR("进程重启失败: {}", process_id);
        info.status = ProcessStatus::Error;
        return false;
    }
}

void ProcessManager::UpdateProcessResources(ProcessInfo& info) {
    if (info.pid <= 0) {
        return;
    }
    
    // 使用 ProcessMonitor 获取资源使用情况
    ProcessResources resources = ProcessMonitor::GetProcessResources(info.pid);
    info.cpu_usage = resources.cpu_usage;
    info.memory_usage = resources.memory_usage;
}

void ProcessManager::ApplyResourceLimits(pid_t pid, int cpu_limit, int64_t memory_limit) {
    // CPU 限制：使用 nice 值调整进程优先级
    // 注意：更精确的 CPU 限制需要使用 cgroups（Linux）或 App Sandbox（macOS）
    // 当前实现使用 nice 值作为简单的优先级调整
    if (cpu_limit > 0 && pid > 0) {
        // 根据 CPU 限制计算 nice 值（0-100% 映射到 nice 0-19）
        // CPU 限制越高，nice 值越小（优先级越高）
        int nice_value = 19 - (cpu_limit * 19 / 100);
        if (nice_value < 0) nice_value = 0;
        if (nice_value > 19) nice_value = 19;
        
        // 使用 setpriority 设置指定进程的优先级
        if (setpriority(PRIO_PROCESS, pid, nice_value) != 0) {
            LOG_WARN("设置进程 {} 优先级失败 (nice={}): {}", pid, nice_value, strerror(errno));
        } else {
            LOG_DEBUG("设置进程 {} CPU 限制: {}% (nice={})", pid, cpu_limit, nice_value);
        }
    }
    
    // 实现内存限制
    if (memory_limit > 0) {
        struct rlimit rlim;
        rlim.rlim_cur = memory_limit;
        rlim.rlim_max = memory_limit;
        if (setrlimit(RLIMIT_AS, &rlim) != 0) {
            LOG_WARN("设置内存限制失败: {}", strerror(errno));
        }
    }
}

// ============================================
// Phase 2.3: 分层监控实现
// ============================================

int ProcessManager::GetTierInterval(MonitorTier tier) const {
    switch (tier) {
        case MonitorTier::Critical:
            return 1;   // 1秒 - Error状态、新启动、高CPU
        case MonitorTier::Normal:
            return 5;   // 5秒 - 常规Running
        case MonitorTier::Low:
            return 30;  // 30秒 - Stopped
        case MonitorTier::Minimal:
            return 60;  // 60秒 - 长期稳定
        default:
            return 5;   // 默认Normal
    }
}

void ProcessManager::UpdateProcessTier(const std::string& process_id) {
    // 注意：调用前已持有mutex_锁
    auto it = processes_.find(process_id);
    if (it == processes_.end()) {
        return;
    }
    
    ProcessInfo& info = it->second;
    MonitorTier old_tier = info.tier;
    MonitorTier new_tier = old_tier;
    
    // 根据进程状态决定tier
    switch (info.status) {
        case ProcessStatus::Error:
        case ProcessStatus::Starting:
        case ProcessStatus::Restarting:
            // Error/Starting/Restarting状态需要密切监控
            new_tier = MonitorTier::Critical;
            break;
            
        case ProcessStatus::Running: {
            // Running状态：根据运行时长和CPU使用率决定
            auto now = std::chrono::system_clock::now();
            auto runtime = std::chrono::duration_cast<std::chrono::seconds>(
                now - info.start_time).count();
            
            if (runtime < 30) {
                // 新启动（<30秒）：标准监控
                new_tier = MonitorTier::Normal;
            } else if (info.cpu_usage > 80.0) {
                // 高CPU使用：密切监控
                new_tier = MonitorTier::Critical;
            } else if (runtime > 300) {  // 5分钟
                // 长期稳定运行：最低监控
                new_tier = MonitorTier::Minimal;
            } else {
                // 其他情况：标准监控
                new_tier = MonitorTier::Normal;
            }
            break;
        }
            
        case ProcessStatus::Stopped:
        case ProcessStatus::Stopping:
            // Stopped/Stopping状态：低频监控
            new_tier = MonitorTier::Low;
            break;
    }
    
    if (new_tier != old_tier) {
        info.tier = new_tier;
        LOG_DEBUG("Phase 2.3: Process {} tier changed: {} -> {} (status: {})", 
                  process_id, static_cast<int>(old_tier), static_cast<int>(new_tier),
                  static_cast<int>(info.status));
    }
}

} // namespace process

