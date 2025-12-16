#include "process/process_monitor.hpp"
#include "utils/logger.hpp"
#include <chrono>
#include <cstring>
#include <errno.h>
#include <fstream>
#include <signal.h>
#include <sstream>
#include <sys/resource.h>
#include <sys/time.h>
#include <unistd.h>
#ifdef __APPLE__
#include <sys/sysctl.h>
#endif

#ifdef __APPLE__
#include <libproc.h>
#include <mach/mach.h>
#include <mach/task_info.h>
#else
#include <sys/sysinfo.h>
#endif

namespace process {

// 静态成员变量初始化
int64_t ProcessMonitor::last_system_cpu_time_ = 0;
int64_t ProcessMonitor::last_process_cpu_time_ = 0;
std::chrono::system_clock::time_point ProcessMonitor::last_update_time_ =
    std::chrono::system_clock::now();
pid_t ProcessMonitor::cached_pid_ = 0;

ProcessResources ProcessMonitor::GetProcessResources(pid_t pid) {
  ProcessResources resources;

  if (pid <= 0) {
    return resources;
  }

  // 获取内存使用
#ifdef __APPLE__
  resources.memory_usage = GetProcessMemoryMacOS(pid);
#else
  resources.memory_usage = GetProcessMemoryLinux(pid);
#endif

  // 获取 CPU 使用率
  auto now = std::chrono::system_clock::now();
  int64_t system_cpu_time = GetSystemCpuTime();
  int64_t process_cpu_time = GetProcessCpuTime(pid);

  if (cached_pid_ == pid && last_system_cpu_time_ > 0 &&
      last_process_cpu_time_ > 0) {
    // 计算时间差
    auto time_diff = std::chrono::duration_cast<std::chrono::milliseconds>(
                         now - last_update_time_)
                         .count();

    if (time_diff > 0) {
      // 计算 CPU 使用率
      int64_t system_diff = system_cpu_time - last_system_cpu_time_;
      int64_t process_diff = process_cpu_time - last_process_cpu_time_;

      if (system_diff > 0) {
        // CPU 使用率 = (进程 CPU 时间差 / 系统 CPU 时间差) * 100
        resources.cpu_usage =
            (static_cast<double>(process_diff) / system_diff) * 100.0;

        // 限制在 0-100% 范围内
        if (resources.cpu_usage < 0.0) {
          resources.cpu_usage = 0.0;
        } else if (resources.cpu_usage > 100.0) {
          resources.cpu_usage = 100.0;
        }
      }
    }
  }

  // 更新缓存
  last_system_cpu_time_ = system_cpu_time;
  last_process_cpu_time_ = process_cpu_time;
  last_update_time_ = now;
  cached_pid_ = pid;

  return resources;
}

bool ProcessMonitor::IsProcessAlive(pid_t pid) {
  if (pid <= 0) {
    return false;
  }

  // 使用 kill(pid, 0) 检查进程是否存在
  if (kill(pid, 0) == 0) {
    return true;
  }

  return errno != ESRCH;
}

int64_t ProcessMonitor::GetSystemCpuTime() {
#ifdef __APPLE__
  // macOS: 使用 host_statistics64 获取系统 CPU 时间
  mach_msg_type_number_t count = HOST_CPU_LOAD_INFO_COUNT;
  host_cpu_load_info_data_t cpu_info;
  kern_return_t result = host_statistics64(mach_host_self(), HOST_CPU_LOAD_INFO,
                                           (host_info64_t)&cpu_info, &count);
  if (result == KERN_SUCCESS) {
    return cpu_info.cpu_ticks[CPU_STATE_USER] +
           cpu_info.cpu_ticks[CPU_STATE_SYSTEM] +
           cpu_info.cpu_ticks[CPU_STATE_NICE] +
           cpu_info.cpu_ticks[CPU_STATE_IDLE];
  }
  return 0;
#else
  // Linux: 读取 /proc/stat
  std::ifstream stat_file("/proc/stat");
  if (!stat_file.is_open()) {
    return 0;
  }

  std::string line;
  if (std::getline(stat_file, line)) {
    std::istringstream iss(line);
    std::string cpu;
    int64_t user, nice, system, idle, iowait, irq, softirq, steal;
    iss >> cpu >> user >> nice >> system >> idle >> iowait >> irq >> softirq >>
        steal;
    return user + nice + system + idle + iowait + irq + softirq + steal;
  }
  return 0;
#endif
}

int64_t ProcessMonitor::GetProcessCpuTime(pid_t pid) {
#ifdef __APPLE__
  // macOS: 使用 proc_pidinfo 获取进程 CPU 时间
  proc_taskinfo task_info;
  int ret =
      proc_pidinfo(pid, PROC_PIDTASKINFO, 0, &task_info, sizeof(task_info));
  if (ret == sizeof(task_info)) {
    // 返回用户时间 + 系统时间（单位：纳秒，转换为 jiffies）
    // macOS 的 jiffies 通常是 100Hz，即 10ms
    return (task_info.pti_total_user + task_info.pti_total_system) / 10000000;
  }
  return 0;
#else
  // Linux: 读取 /proc/[pid]/stat
  std::ostringstream stat_path;
  stat_path << "/proc/" << pid << "/stat";
  std::ifstream stat_file(stat_path.str());
  if (!stat_file.is_open()) {
    return 0;
  }

  std::string line;
  if (std::getline(stat_file, line)) {
    std::istringstream iss(line);
    std::string token;
    int64_t utime = 0, stime = 0;

    // 跳过前 13 个字段，读取 utime (14) 和 stime (15)
    for (int i = 0; i < 13; ++i) {
      iss >> token;
    }
    iss >> utime >> stime;
    return utime + stime;
  }
  return 0;
#endif
}

int64_t ProcessMonitor::GetProcessMemoryMacOS(pid_t pid) {
#ifdef __APPLE__
  struct proc_taskinfo task_info;
  int ret =
      proc_pidinfo(pid, PROC_PIDTASKINFO, 0, &task_info, sizeof(task_info));
  if (ret == sizeof(task_info)) {
    // 返回物理内存使用（RSS）
    return task_info.pti_resident_size;
  }
  return 0;
#else
  (void)pid;
  return 0;
#endif
}

int64_t ProcessMonitor::GetProcessMemoryLinux(pid_t pid) {
  std::ostringstream status_path;
  status_path << "/proc/" << pid << "/status";
  std::ifstream status_file(status_path.str());
  if (!status_file.is_open()) {
    return 0;
  }

  std::string line;
  while (std::getline(status_file, line)) {
    if (line.find("VmRSS:") == 0) {
      // 格式: VmRSS:    12345 kB
      std::istringstream iss(line);
      std::string key, value, unit;
      iss >> key >> value >> unit;
      if (unit == "kB") {
        return std::stoll(value) * 1024; // 转换为字节
      }
    }
  }
  return 0;
}

ProcessMonitor::SystemResources ProcessMonitor::GetSystemResources() {
  SystemResources resources;

#ifdef __APPLE__
  // macOS Implementation

  // 1. Get Memory Usage
  mach_msg_type_number_t count = HOST_VM_INFO64_COUNT;
  vm_statistics64_data_t vm_info;
  if (host_statistics64(mach_host_self(), HOST_VM_INFO64,
                        (host_info64_t)&vm_info, &count) == KERN_SUCCESS) {
    // Page size is usually 4KB or 16KB
    vm_size_t page_size;
    host_page_size(mach_host_self(), &page_size);

    // Used memory = active + wired (simplified)
    // Note: This is a rough estimate. macOS memory management is complex
    // (compressed, etc.)
    resources.memory_usage =
        (int64_t)(vm_info.active_count + vm_info.wire_count) * page_size;
  }

  // 2. Get Total Memory
  int mib[2];
  mib[0] = CTL_HW;
  mib[1] = HW_MEMSIZE;
  int64_t total_memory = 0;
  size_t length = sizeof(int64_t);
  if (sysctl(mib, 2, &total_memory, &length, NULL, 0) == 0) {
    resources.total_memory = total_memory;
  }

  // 3. Get CPU Usage
  // Use static variables to track previous state
  static int64_t prev_total = 0;
  static int64_t prev_idle = 0;

  count = HOST_CPU_LOAD_INFO_COUNT;
  host_cpu_load_info_data_t cpu_info;
  if (host_statistics64(mach_host_self(), HOST_CPU_LOAD_INFO,
                        (host_info64_t)&cpu_info, &count) == KERN_SUCCESS) {
    int64_t total = 0;
    for (int i = 0; i < CPU_STATE_MAX; i++) {
      total += cpu_info.cpu_ticks[i];
    }
    int64_t idle = cpu_info.cpu_ticks[CPU_STATE_IDLE];

    if (prev_total > 0) {
      int64_t total_diff = total - prev_total;
      int64_t idle_diff = idle - prev_idle;

      if (total_diff > 0) {
        resources.cpu_usage = 100.0 * (total_diff - idle_diff) / total_diff;
      }
    }

    prev_total = total;
    prev_idle = idle;
  }

#else
  // Linux Implementation

  // 1. Get Memory Usage
  std::ifstream meminfo("/proc/meminfo");
  if (meminfo.is_open()) {
    std::string line;
    int64_t mem_total = 0;
    int64_t mem_available = 0;

    while (std::getline(meminfo, line)) {
      std::istringstream iss(line);
      std::string key;
      int64_t value;
      std::string unit;
      iss >> key >> value >> unit;

      if (key == "MemTotal:") {
        mem_total = value * 1024;
      } else if (key == "MemAvailable:") {
        mem_available = value * 1024;
      }
    }

    resources.total_memory = mem_total;
    resources.memory_usage = mem_total - mem_available;
  }

  // 2. Get CPU Usage
  static int64_t prev_total = 0;
  static int64_t prev_idle = 0;

  std::ifstream stat_file("/proc/stat");
  if (stat_file.is_open()) {
    std::string line;
    if (std::getline(stat_file, line)) {
      std::istringstream iss(line);
      std::string cpu;
      int64_t user, nice, system, idle, iowait, irq, softirq, steal;
      iss >> cpu >> user >> nice >> system >> idle >> iowait >> irq >>
          softirq >> steal;

      int64_t total =
          user + nice + system + idle + iowait + irq + softirq + steal;
      int64_t idle_all = idle + iowait;

      if (prev_total > 0) {
        int64_t total_diff = total - prev_total;
        int64_t idle_diff = idle_all - prev_idle;

        if (total_diff > 0) {
          resources.cpu_usage = 100.0 * (total_diff - idle_diff) / total_diff;
        }
      }

      prev_total = total;
      prev_idle = idle_all;
    }
  }
#endif

  return resources;
}

} // namespace process
