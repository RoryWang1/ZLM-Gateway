#ifndef UTILS_LOGGER_HPP
#define UTILS_LOGGER_HPP

#include <string>
#include <memory>
#include <spdlog/spdlog.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/sinks/rotating_file_sink.h>
#include <spdlog/sinks/basic_file_sink.h>

namespace utils {

/**
 * @brief 日志管理器
 */
class Logger {
public:
    /**
     * @brief 初始化日志系统
     * @param level 日志级别: "trace", "debug", "info", "warn", "error", "critical", "off"
     * @param log_file 日志文件路径（可选，为空则只输出到控制台）
     * @param max_size 最大文件大小（MB）
     * @param max_files 最大文件数量
     */
    static void Initialize(const std::string& level = "info",
                          const std::string& log_file = "",
                          size_t max_size = 100,
                          size_t max_files = 5);

    /**
     * @brief 获取全局日志实例
     */
    static std::shared_ptr<spdlog::logger> Get();

    /**
     * @brief 设置日志级别
     */
    static void SetLevel(const std::string& level);

private:
    static std::shared_ptr<spdlog::logger> logger_;
    static spdlog::level::level_enum ParseLevel(const std::string& level);
};

// 便捷宏定义（使用全局命名空间，避免在namespace内部解析错误）
#define LOG_TRACE(...)    ::utils::Logger::Get()->trace(__VA_ARGS__)
#define LOG_DEBUG(...)    ::utils::Logger::Get()->debug(__VA_ARGS__)
#define LOG_INFO(...)     ::utils::Logger::Get()->info(__VA_ARGS__)
#define LOG_WARN(...)     ::utils::Logger::Get()->warn(__VA_ARGS__)
#define LOG_ERROR(...)    ::utils::Logger::Get()->error(__VA_ARGS__)
#define LOG_CRITICAL(...) utils::Logger::Get()->critical(__VA_ARGS__)

} // namespace utils

#endif // UTILS_LOGGER_HPP

