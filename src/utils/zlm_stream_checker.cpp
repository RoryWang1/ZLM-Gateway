#include "utils/zlm_stream_checker.hpp"
#include "streaming/zlmediakit/zlm_client.hpp"
#include <thread>
#include <chrono>

namespace utils {

bool ZLMStreamChecker::IsStreamAlive(std::shared_ptr<streaming::ZLMClient> zlm_client,
                                      const std::string& target_app,
                                      const std::string& target_stream) {
    if (!zlm_client) {
        return false;
    }

    auto stream_info = zlm_client->GetStreamInfo(target_app, target_stream);
    // 使用 ZLMClient::IsStreamActive 进行更全面的判断
    // 不仅检查 alive 字段，还检查 bytes_speed 和 total_bytes
    // 这样可以避免因为 ZLM 的 alive 字段延迟更新而导致的误判
    return streaming::ZLMClient::IsStreamActive(stream_info);
}

bool ZLMStreamChecker::WaitForStreamAlive(std::shared_ptr<streaming::ZLMClient> zlm_client,
                                          const std::string& target_app,
                                          const std::string& target_stream,
                                          int check_interval_ms,
                                          int timeout_ms,
                                          std::function<void(int elapsed_ms)> on_check) {
    if (!zlm_client) {
        return false;
    }

    int total_wait_time = 0;
    int check_interval = check_interval_ms;
    int remaining_timeout = timeout_ms;

    while (total_wait_time < timeout_ms) {
        if (IsStreamAlive(zlm_client, target_app, target_stream)) {
            return true;
        }

        if (on_check) {
            on_check(total_wait_time);
        }

        if (total_wait_time + check_interval > timeout_ms) {
            check_interval = timeout_ms - total_wait_time;
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(check_interval));
        total_wait_time += check_interval;

        // 动态调整检查间隔（第二次检查后间隔加倍）
        if (total_wait_time >= check_interval_ms * 2 && check_interval == check_interval_ms) {
            remaining_timeout = timeout_ms - total_wait_time;
            check_interval = std::min(remaining_timeout, check_interval_ms * 2);
        }
    }

    return false;
}

} // namespace utils

