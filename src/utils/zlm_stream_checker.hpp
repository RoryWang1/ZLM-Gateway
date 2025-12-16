#ifndef UTILS_ZLM_STREAM_CHECKER_HPP
#define UTILS_ZLM_STREAM_CHECKER_HPP

#include <string>
#include <memory>
#include <chrono>
#include <functional>

namespace streaming {
class ZLMClient;
}

namespace utils {

/**
 * @brief ZLM 流检查工具类
 * 
 * 统一封装检查 ZLM 中流是否存在的逻辑，减少重复代码
 */
class ZLMStreamChecker {
public:
    /**
     * @brief 检查 ZLM 中流是否存在且活跃
     * @param zlm_client ZLM 客户端
     * @param target_app 目标应用名
     * @param target_stream 目标流名
     * @return 流是否存在且活跃
     */
    static bool IsStreamAlive(std::shared_ptr<streaming::ZLMClient> zlm_client,
                              const std::string& target_app,
                              const std::string& target_stream);

    /**
     * @brief 等待流在 ZLM 中变为活跃状态
     * @param zlm_client ZLM 客户端
     * @param target_app 目标应用名
     * @param target_stream 目标流名
     * @param check_interval_ms 检查间隔（毫秒）
     * @param timeout_ms 超时时间（毫秒）
     * @param on_check 每次检查时的回调函数（可选）
     * @return 是否成功（流在超时前变为活跃）
     */
    static bool WaitForStreamAlive(std::shared_ptr<streaming::ZLMClient> zlm_client,
                                   const std::string& target_app,
                                   const std::string& target_stream,
                                   int check_interval_ms = 1000,
                                   int timeout_ms = 10000,
                                   std::function<void(int elapsed_ms)> on_check = nullptr);
};

} // namespace utils

#endif // UTILS_ZLM_STREAM_CHECKER_HPP

