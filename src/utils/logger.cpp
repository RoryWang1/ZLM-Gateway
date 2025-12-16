#include "utils/logger.hpp"
#include <filesystem>
#include <vector>
#include <iostream>

namespace utils {

std::shared_ptr<spdlog::logger> Logger::logger_ = nullptr;

void Logger::Initialize(const std::string& level,
                       const std::string& log_file,
                       size_t max_size,
                       size_t max_files) {
    std::vector<spdlog::sink_ptr> sinks;

    // 控制台输出 - 在开发/调试模式下启用
    // 注意：在生产环境中可以禁用，但在调试时很有用
    auto console_sink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
    console_sink->set_level(spdlog::level::trace);
    console_sink->set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%^%l%$] [%t] %v");
    sinks.push_back(console_sink);

    // 文件输出（如果指定了日志文件）
    if (!log_file.empty()) {
        try {
            // 确保日志目录存在
            std::filesystem::path log_path(log_file);
            auto log_dir = log_path.parent_path();
            if (!log_dir.empty() && !std::filesystem::exists(log_dir)) {
                std::filesystem::create_directories(log_dir);
            }

            // 使用 rotating file sink
            auto file_sink = std::make_shared<spdlog::sinks::rotating_file_sink_mt>(
                log_file, max_size * 1024 * 1024, max_files);
            file_sink->set_level(spdlog::level::trace);
            file_sink->set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%l] [%t] %v");
            sinks.push_back(file_sink);
        } catch (const std::exception& e) {
            // 如果文件日志初始化失败，记录错误（使用临时控制台输出）
            // 注意：这里不能使用 LOG_* 宏，因为 logger 还没有初始化
            std::cerr << "[Logger] 警告: 无法初始化文件日志 (" << log_file << "): " << e.what() << std::endl;
        }
    }

    // 如果没有任何sink（日志文件创建失败），至少添加一个控制台sink避免崩溃
    if (sinks.empty()) {
        auto console_sink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
        console_sink->set_level(spdlog::level::trace);
        console_sink->set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%^%l%$] [%t] %v");
        sinks.push_back(console_sink);
    }

    // 创建 logger
    logger_ = std::make_shared<spdlog::logger>("gateway", sinks.begin(), sinks.end());
    logger_->set_level(ParseLevel(level));
    logger_->flush_on(spdlog::level::warn);

    // 注册为默认 logger
    spdlog::register_logger(logger_);
    spdlog::set_default_logger(logger_);
}

std::shared_ptr<spdlog::logger> Logger::Get() {
    if (!logger_) {
        // 如果未初始化，使用默认控制台 logger
        Initialize();
    }
    return logger_;
}

void Logger::SetLevel(const std::string& level) {
    if (logger_) {
        logger_->set_level(ParseLevel(level));
    }
}

spdlog::level::level_enum Logger::ParseLevel(const std::string& level) {
    if (level == "trace") return spdlog::level::trace;
    if (level == "debug") return spdlog::level::debug;
    if (level == "info") return spdlog::level::info;
    if (level == "warn") return spdlog::level::warn;
    if (level == "error") return spdlog::level::err;
    if (level == "critical") return spdlog::level::critical;
    if (level == "off") return spdlog::level::off;
    return spdlog::level::info; // 默认 info
}

} // namespace utils

