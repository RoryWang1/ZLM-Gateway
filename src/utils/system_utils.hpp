#pragma once

#include <string>

namespace utils {

class SystemUtils {
public:
    /**
     * @brief 检查端口占用并尝试清理（杀掉占用端口的进程）
     * 
     * 会排除当前进程自身。
     * 如果发现占用端口的进程是 gateway_manager（但不是当前进程），会强制杀死。
     * 
     * @param port 端口号
     */
    static void CheckAndCleanPort(int port);
};

} // namespace utils
