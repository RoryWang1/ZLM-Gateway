#ifndef GATEWAY_GB28181_GB28181_DEVICE_HPP
#define GATEWAY_GB28181_GB28181_DEVICE_HPP

#include <string>
#include <vector>
#include <chrono>

namespace gateway {

/**
 * @brief GB28181 设备信息
 */
struct GB28181Device {
    std::string id;                      // 设备ID（20位国标ID）
    std::string name;                     // 设备名称
    std::string manufacturer;             // 制造商
    std::string model;                    // 型号
    std::string ip;                       // 设备IP地址
    int port = 5060;                      // SIP端口
    std::string username;                 // 用户名（用于认证）
    std::string password;                 // 密码（用于认证）
    std::vector<std::string> channels;   // 通道列表
    std::chrono::system_clock::time_point last_seen;  // 最后心跳时间
    std::chrono::system_clock::time_point register_time;  // 注册时间
    bool online = false;                  // 是否在线
    
    /**
     * @brief 检查设备是否过期（心跳超时）
     */
    bool IsExpired(int timeout_seconds) const {
        if (!online) {
            return true;
        }
        auto now = std::chrono::system_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - last_seen).count();
        return elapsed > timeout_seconds;
    }
};

} // namespace gateway

#endif // GATEWAY_GB28181_GB28181_DEVICE_HPP

