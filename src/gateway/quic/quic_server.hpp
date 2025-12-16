#ifndef GATEWAY_QUIC_QUIC_SERVER_HPP
#define GATEWAY_QUIC_QUIC_SERVER_HPP

#include <string>
#include <memory>
#include <functional>
#include <thread>
#include <atomic>
#include <mutex>
#include <map>
#include <cstdint>

// msquic 头文件（前向声明）
#ifdef HAVE_MSQUIC
struct QUIC_API_TABLE;
typedef struct QUIC_API_TABLE* const QUIC_API_TABLE_PTR;
typedef void* HQUIC;
struct QUIC_LISTENER_EVENT;
struct QUIC_CONNECTION_EVENT;
struct QUIC_STREAM_EVENT;
typedef uint32_t QUIC_STATUS;
#define QUIC_API
#define QUIC_FAILED(Status) ((Status) != 0)
#define QUIC_STATUS_SUCCESS 0
#endif

namespace gateway {
namespace quic {

/**
 * @brief QUIC 连接类
 * 
 * 表示一个 QUIC 连接，用于接收数据流
 */
class QuicConnection {
public:
    QuicConnection(uint64_t connection_id, uint64_t stream_id);
    ~QuicConnection();

    /**
     * @brief 从 QUIC 流接收数据
     * @param buffer 接收缓冲区
     * @param max_len 最大长度
     * @return 实际接收的字节数
     */
    size_t Receive(uint8_t* buffer, size_t max_len);

    /**
     * @brief 获取流 ID
     * @return 流 ID
     */
    uint64_t GetStreamId() const { return stream_id_; }

    /**
     * @brief 获取连接 ID
     * @return 连接 ID
     */
    uint64_t GetConnectionId() const { return connection_id_; }

    /**
     * @brief 检查连接是否有效
     * @return 是否有效
     */
    bool IsValid() const { return is_valid_; }

    /**
     * @brief 关闭连接
     */
    void Close();

    /**
     * @brief 设置数据接收回调
     * @param callback 回调函数，参数为 (data, len)
     */
    void SetDataCallback(std::function<void(const uint8_t*, size_t)> callback);

private:
    uint64_t connection_id_;
    uint64_t stream_id_;
    std::atomic<bool> is_valid_;
    std::function<void(const uint8_t*, size_t)> data_callback_;
    std::mutex callback_mutex_;
};

/**
 * @brief QUIC 服务器类
 * 
 * 接收 QUIC 连接并管理 QUIC 流
 */
class QuicServer {
public:
    /**
     * @brief 构造函数
     * @param bind_addr 绑定地址
     * @param port 端口
     */
    QuicServer(const std::string& bind_addr, uint16_t port);

    /**
     * @brief 析构函数
     */
    ~QuicServer();

    /**
     * @brief 启动服务器
     * @return 是否成功
     */
    bool Start();

    /**
     * @brief 停止服务器
     */
    void Stop();

    /**
     * @brief 检查服务器是否运行中
     * @return 是否运行中
     */
    bool IsRunning() const { return is_running_; }

    /**
     * @brief 设置新连接回调
     * @param callback 回调函数，参数为 QuicConnection*
     */
    void SetConnectionCallback(
        std::function<void(std::shared_ptr<QuicConnection>)> callback);

    /**
     * @brief 获取绑定的地址
     * @return 地址
     */
    std::string GetBindAddress() const { return bind_addr_; }

    /**
     * @brief 获取端口
     * @return 端口
     */
    uint16_t GetPort() const { return port_; }

private:
    /**
     * @brief 服务器主循环
     */
    void ServerLoop();

    /**
     * @brief 处理新的 QUIC 数据包
     * @param data 数据包
     * @param len 长度
     * @param peer_addr 对端地址
     */
    void HandlePacket(const uint8_t* data, size_t len, const std::string& peer_addr);
    
#ifdef HAVE_MSQUIC
    /**
     * @brief 初始化 msquic
     * @return 是否成功
     */
    bool InitializeMsQuic();
    
    /**
     * @brief 清理 msquic 资源
     */
    void CleanupMsQuic();
    
    /**
     * @brief msquic 监听器回调
     */
    static QUIC_STATUS QUIC_API ListenerCallback(
        HQUIC Listener,
        void* Context,
        QUIC_LISTENER_EVENT* Event);
    
    /**
     * @brief msquic 连接回调
     */
    static QUIC_STATUS QUIC_API ConnectionCallback(
        HQUIC Connection,
        void* Context,
        QUIC_CONNECTION_EVENT* Event);
    
    /**
     * @brief msquic 流回调
     */
    static QUIC_STATUS QUIC_API StreamCallback(
        HQUIC Stream,
        void* Context,
        QUIC_STREAM_EVENT* Event);
    
    // msquic 相关成员
    const QUIC_API_TABLE* msquic_api_;
    HQUIC msquic_registration_;
    HQUIC msquic_configuration_;
    HQUIC msquic_listener_;
    bool msquic_initialized_;
#endif

    std::string bind_addr_;
    uint16_t port_;
    std::atomic<bool> is_running_;
    std::thread server_thread_;
    int socket_fd_;
    
    std::function<void(std::shared_ptr<QuicConnection>)> connection_callback_;
    std::mutex callback_mutex_;
    
    // 连接管理
    std::map<uint64_t, std::shared_ptr<QuicConnection>> connections_;
    std::mutex connections_mutex_;
    uint64_t next_connection_id_;
};

} // namespace quic
} // namespace gateway

#endif // GATEWAY_QUIC_QUIC_SERVER_HPP

