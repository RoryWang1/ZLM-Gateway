#ifndef PROCESS_PROCESS_MONITOR_HPP
#define PROCESS_PROCESS_MONITOR_HPP

#include <cstdint>
#include <string>
#include <chrono>
#include <unistd.h>
#include <signal.h>
#include <errno.h>

namespace process {

/**
 * @brief 进程资源使用情况
 */
struct ProcessResources {
    double cpu_usage = 0.0;      // CPU 使用率（百分比）
    int64_t memory_usage = 0;    // 内存使用（字节）
    int64_t virtual_memory = 0;  // 虚拟内存（字节）
    int64_t rss = 0;             // 物理内存（RSS，字节）
};

/**
 * @brief 进程资源监控工具类
 */
class ProcessMonitor {
public:
    /**
     * @brief 获取进程资源使用情况
     * @param pid 进程 PID
     * @return 资源使用情况
     */
    static ProcessResources GetProcessResources(pid_t pid);
    
    /**
     * @brief 检查进程是否存在
     * @param pid 进程 PID
     * @return 是否存在
     */
    static bool IsProcessAlive(pid_t pid);

    /**
     * @brief 系统资源使用情况
     */
    struct SystemResources {
        double cpu_usage = 0.0;      // 系统 CPU 使用率（百分比）
        int64_t memory_usage = 0;    // 系统内存使用（字节）
        int64_t total_memory = 0;    // 系统总内存（字节）
    };

    /**
     * @brief 获取系统资源使用情况
     * @return 系统资源使用情况
     */
    static SystemResources GetSystemResources();
    
private:
    /**
     * @brief 获取系统总 CPU 时间（用于计算 CPU 使用率）
     * @return CPU 时间（jiffies）
     */
    static int64_t GetSystemCpuTime();
    
    /**
     * @brief 获取进程 CPU 时间
     * @param pid 进程 PID
     * @return CPU 时间（jiffies）
     */
    static int64_t GetProcessCpuTime(pid_t pid);
    
    /**
     * @brief 获取进程内存使用（macOS）
     * @param pid 进程 PID
     * @return 内存使用（字节）
     */
    static int64_t GetProcessMemoryMacOS(pid_t pid);
    
    /**
     * @brief 获取进程内存使用（Linux）
     * @param pid 进程 PID
     * @return 内存使用（字节）
     */
    static int64_t GetProcessMemoryLinux(pid_t pid);
    
    // CPU 使用率计算缓存
    static int64_t last_system_cpu_time_;
    static int64_t last_process_cpu_time_;
    static std::chrono::system_clock::time_point last_update_time_;
    static pid_t cached_pid_;
};

} // namespace process

#endif // PROCESS_PROCESS_MONITOR_HPP

