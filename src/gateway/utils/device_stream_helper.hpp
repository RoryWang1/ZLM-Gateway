#ifndef GATEWAY_UTILS_DEVICE_STREAM_HELPER_HPP
#define GATEWAY_UTILS_DEVICE_STREAM_HELPER_HPP

#include <string>
#include <vector>
#include <memory>
#include <map>
#include <mutex>
#include "gateway/base/gateway_base.hpp"

namespace streaming {
class ZLMClient;
class StreamManager;
}

namespace gateway {
namespace utils {

/**
 * @brief 设备流管理辅助工具
 * 
 * 提供统一的设备流启动和停止逻辑，减少重复代码
 */
template<typename StreamInfoType>
class DeviceStreamHelper {
public:
    /**
     * @brief 启动设备流
     * @param stream_id 流ID
     * @param device_id 设备ID
     * @param channel_id 通道ID
     * @param target_app 目标应用名
     * @param target_stream 目标流名
     * @param streams 流映射
     * @param mutex 互斥锁
     * @param zlm_client ZLM客户端
     * @param stream_manager 流管理器（可选）
     * @param get_rtsp_urls_callback 获取RTSP URL列表的回调函数
     * @param gateway_name Gateway名称（用于日志）
     * @param protocol 协议名称（用于状态机，如 "isapi"、"dahua"、"psia"）
     * @return 是否成功
     */
    static bool StartDeviceStream(
        const std::string& stream_id,
        const std::string& device_id,
        const std::string& channel_id,
        const std::string& target_app,
        const std::string& target_stream,
        std::map<std::string, StreamInfoType>& streams,
        std::mutex& mutex,
        std::shared_ptr<streaming::ZLMClient> zlm_client,
        std::shared_ptr<streaming::StreamManager> stream_manager,
        std::function<std::vector<std::string>(const std::string&, const std::string&)> get_rtsp_urls_callback,
        const std::string& gateway_name,
        const std::string& protocol);

    /**
     * @brief 停止设备流
     * @param stream_id 流ID
     * @param target_app 目标应用名
     * @param target_stream 目标流名
     * @param streams 流映射
     * @param mutex 互斥锁
     * @param zlm_client ZLM客户端
     * @param gateway_name Gateway名称（用于日志）
     * @return 是否成功
     */
    static bool StopDeviceStream(
        const std::string& stream_id,
        const std::string& target_app,
        const std::string& target_stream,
        std::map<std::string, StreamInfoType>& streams,
        std::mutex& mutex,
        std::shared_ptr<streaming::ZLMClient> zlm_client,
        const std::string& gateway_name);
};

} // namespace utils
} // namespace gateway

#include "gateway/utils/device_stream_helper_impl.hpp"

#endif // GATEWAY_UTILS_DEVICE_STREAM_HELPER_HPP

