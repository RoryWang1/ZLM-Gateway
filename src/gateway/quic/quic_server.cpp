#include "gateway/quic/quic_server.hpp"
#include "utils/logger.hpp"
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <cstring>

// msquic 头文件
#ifdef HAVE_MSQUIC
#include <msquic.h>
#include <msquic_winuser.h>  // 平台无关的辅助函数
#endif

namespace gateway {
namespace quic {

// QuicConnection 实现
QuicConnection::QuicConnection(uint64_t connection_id, uint64_t stream_id)
    : connection_id_(connection_id), stream_id_(stream_id), is_valid_(true) {
}

QuicConnection::~QuicConnection() {
    Close();
}

size_t QuicConnection::Receive(uint8_t* buffer, size_t max_len) {
    if (!is_valid_) {
        return 0;
    }
    
    // TODO: 实现真正的 QUIC 数据接收
    // 这里需要集成 quiche 或 msquic 库
    // 目前返回 0 表示未实现
    return 0;
}

void QuicConnection::Close() {
    if (is_valid_.exchange(false)) {
        LOG_DEBUG("[QuicConnection] 关闭连接: connection_id={}, stream_id={}", 
                 connection_id_, stream_id_);
    }
}

void QuicConnection::SetDataCallback(std::function<void(const uint8_t*, size_t)> callback) {
    std::lock_guard<std::mutex> lock(callback_mutex_);
    data_callback_ = callback;
}

// QuicServer 实现
QuicServer::QuicServer(const std::string& bind_addr, uint16_t port)
    : bind_addr_(bind_addr), port_(port), is_running_(false), 
      socket_fd_(-1), next_connection_id_(1)
#ifdef HAVE_MSQUIC
      , msquic_api_(nullptr), msquic_registration_(nullptr),
      msquic_configuration_(nullptr), msquic_listener_(nullptr),
      msquic_initialized_(false)
#endif
{
}

QuicServer::~QuicServer() {
    Stop();
#ifdef HAVE_MSQUIC
    CleanupMsQuic();
#endif
}

bool QuicServer::Start() {
    if (is_running_) {
        LOG_WARN("[QuicServer] 服务器已在运行");
        return true;
    }

#ifdef HAVE_MSQUIC
    // 使用 msquic 实现
    if (!InitializeMsQuic()) {
        LOG_ERROR("[QuicServer] 初始化 msquic 失败");
        return false;
    }
    
    // 创建监听器
    // 注意：msquic API 可能有所不同，需要根据实际 API 调整
    if (QUIC_FAILED(msquic_api_->ListenerOpen(msquic_registration_, 
                                               ListenerCallback, 
                                               this, 
                                               &msquic_listener_))) {
        LOG_ERROR("[QuicServer] 创建监听器失败");
        CleanupMsQuic();
        return false;
    }
    
    // 配置地址
    QUIC_ADDR Address;
    QuicAddrSetFamily(&Address, QUIC_ADDRESS_FAMILY_INET);
    QuicAddrSetPort(&Address, port_);
    
    // 设置 ALPN（应用层协议协商）
    QUIC_BUFFER AlpnBuffer = { sizeof("quic-gateway") - 1, (uint8_t*)"quic-gateway" };
    if (QUIC_FAILED(msquic_api_->ListenerStart(msquic_listener_, 
                                                &AlpnBuffer, 
                                                1, 
                                                &Address))) {
        LOG_ERROR("[QuicServer] 启动监听器失败");
        msquic_api_->ListenerClose(msquic_listener_);
        CleanupMsQuic();
        return false;
    }
    
    is_running_ = true;
    LOG_INFO("[QuicServer] 启动成功（使用 msquic），监听 {}:{}", bind_addr_, port_);
    return true;
#else
    // 回退到基础 UDP socket 实现（用于开发/测试）
    // 创建 UDP socket（QUIC 基于 UDP）
    socket_fd_ = socket(AF_INET, SOCK_DGRAM, 0);
    if (socket_fd_ < 0) {
        LOG_ERROR("[QuicServer] 创建 socket 失败: {}", strerror(errno));
        return false;
    }

    // 设置 socket 选项
    int opt = 1;
    if (setsockopt(socket_fd_, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
        LOG_ERROR("[QuicServer] 设置 SO_REUSEADDR 失败: {}", strerror(errno));
        close(socket_fd_);
        socket_fd_ = -1;
        return false;
    }

    // 绑定地址
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port_);
    
    if (bind_addr_ == "0.0.0.0" || bind_addr_.empty()) {
        addr.sin_addr.s_addr = INADDR_ANY;
    } else {
        if (inet_aton(bind_addr_.c_str(), &addr.sin_addr) == 0) {
            LOG_ERROR("[QuicServer] 无效的绑定地址: {}", bind_addr_);
            close(socket_fd_);
            socket_fd_ = -1;
            return false;
        }
    }

    if (bind(socket_fd_, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        LOG_ERROR("[QuicServer] 绑定地址失败: {}:{} - {}", 
                 bind_addr_, port_, strerror(errno));
        close(socket_fd_);
        socket_fd_ = -1;
        return false;
    }

    // 设置非阻塞模式
    int flags = fcntl(socket_fd_, F_GETFL, 0);
    if (flags < 0 || fcntl(socket_fd_, F_SETFL, flags | O_NONBLOCK) < 0) {
        LOG_WARN("[QuicServer] 设置非阻塞模式失败: {}", strerror(errno));
    }

    is_running_ = true;
    server_thread_ = std::thread(&QuicServer::ServerLoop, this);

    LOG_INFO("[QuicServer] 启动成功（基础 UDP 模式），监听 {}:{}", bind_addr_, port_);
    return true;
#endif
}

void QuicServer::Stop() {
    if (!is_running_) {
        return;
    }

    is_running_ = false;

#ifdef HAVE_MSQUIC
    // 使用 msquic 实现
    if (msquic_listener_) {
        msquic_api_->ListenerClose(msquic_listener_);
        msquic_listener_ = nullptr;
    }
    
    // 关闭所有连接
    {
        std::lock_guard<std::mutex> lock(connections_mutex_);
        for (auto& pair : connections_) {
            pair.second->Close();
        }
        connections_.clear();
    }
#else
    // 关闭所有连接
    {
        std::lock_guard<std::mutex> lock(connections_mutex_);
        for (auto& pair : connections_) {
            pair.second->Close();
        }
        connections_.clear();
    }

    // 关闭 socket
    if (socket_fd_ >= 0) {
        close(socket_fd_);
        socket_fd_ = -1;
    }

    // 等待服务器线程结束
    if (server_thread_.joinable()) {
        server_thread_.join();
    }
#endif

    LOG_INFO("[QuicServer] 已停止");
}

void QuicServer::SetConnectionCallback(
    std::function<void(std::shared_ptr<QuicConnection>)> callback) {
    std::lock_guard<std::mutex> lock(callback_mutex_);
    connection_callback_ = callback;
}

void QuicServer::ServerLoop() {
    const size_t buffer_size = 65536;  // QUIC 最大包大小
    uint8_t buffer[buffer_size];
    
    while (is_running_) {
        struct sockaddr_in peer_addr;
        socklen_t addr_len = sizeof(peer_addr);
        
        ssize_t received = recvfrom(socket_fd_, buffer, buffer_size, 0,
                                   (struct sockaddr*)&peer_addr, &addr_len);
        
        if (received < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                // 非阻塞模式下没有数据，短暂休眠
                usleep(1000);  // 1ms
                continue;
            } else if (errno == EINTR) {
                continue;
            } else {
                LOG_ERROR("[QuicServer] 接收数据失败: {}", strerror(errno));
                break;
            }
        }

        if (received > 0) {
            char peer_addr_str[INET_ADDRSTRLEN];
            inet_ntop(AF_INET, &peer_addr.sin_addr, peer_addr_str, INET_ADDRSTRLEN);
            std::string peer_addr_full = std::string(peer_addr_str) + ":" + 
                                        std::to_string(ntohs(peer_addr.sin_port));
            
            HandlePacket(buffer, received, peer_addr_full);
        }
    }
}

void QuicServer::HandlePacket(const uint8_t* data, size_t len, const std::string& peer_addr) {
    // TODO: 实现真正的 QUIC 包解析
    // 这里需要集成 quiche 或 msquic 库来解析 QUIC 协议
    
    // 临时实现：为每个新的 peer 创建连接
    // 实际应该根据 QUIC 连接 ID 来管理连接
    
    LOG_DEBUG("[QuicServer] 收到数据包: {} 字节，来自 {}", len, peer_addr);
    
    // 这里应该：
    // 1. 解析 QUIC 包头，获取连接 ID
    // 2. 查找或创建对应的连接
    // 3. 处理 QUIC 握手和数据流
    // 4. 调用连接回调
    
    // 临时实现：为演示目的，创建一个简单的连接
    // 实际实现需要使用 msquic
}

#ifdef HAVE_MSQUIC
bool QuicServer::InitializeMsQuic() {
    if (msquic_initialized_) {
        return true;
    }
    
    // 打开 msquic API
    if (QUIC_FAILED(MsQuicOpen2(&msquic_api_))) {
        LOG_ERROR("[QuicServer] 打开 msquic API 失败");
        return false;
    }
    
    // 创建注册
    QUIC_REGISTRATION_CONFIG RegConfig = { "QUICGateway", QUIC_APPLICATION_FLAG_NONE };
    if (QUIC_FAILED(msquic_api_->RegistrationOpen(&RegConfig, &msquic_registration_))) {
        LOG_ERROR("[QuicServer] 创建注册失败");
        MsQuicClose(msquic_api_);
        msquic_api_ = nullptr;
        return false;
    }
    
    // 创建配置（使用默认配置，实际应该从配置文件读取）
    QUIC_BUFFER AlpnBuffer = { sizeof("quic-gateway") - 1, (uint8_t*)"quic-gateway" };
    QUIC_CREDENTIAL_CONFIG CredConfig;
    memset(&CredConfig, 0, sizeof(CredConfig));
    CredConfig.Type = QUIC_CREDENTIAL_TYPE_NONE;  // 无 TLS（用于测试，生产环境需要证书）
    CredConfig.Flags = QUIC_CREDENTIAL_FLAG_CLIENT | QUIC_CREDENTIAL_FLAG_NO_CERTIFICATE_VALIDATION;
    
    QUIC_CONFIGURATION ConfigSettings;
    memset(&ConfigSettings, 0, sizeof(ConfigSettings));
    
    if (QUIC_FAILED(msquic_api_->ConfigurationOpen(msquic_registration_,
                                                   &AlpnBuffer, 1,
                                                   &ConfigSettings,
                                                   sizeof(ConfigSettings),
                                                   nullptr,
                                                   &msquic_configuration_))) {
        LOG_ERROR("[QuicServer] 创建配置失败");
        msquic_api_->RegistrationClose(msquic_registration_);
        MsQuicClose(msquic_api_);
        msquic_registration_ = nullptr;
        msquic_api_ = nullptr;
        return false;
    }
    
    msquic_initialized_ = true;
    LOG_DEBUG("[QuicServer] msquic 初始化成功");
    return true;
}

void QuicServer::CleanupMsQuic() {
    if (!msquic_initialized_) {
        return;
    }
    
    if (msquic_configuration_) {
        msquic_api_->ConfigurationClose(msquic_configuration_);
        msquic_configuration_ = nullptr;
    }
    
    if (msquic_registration_) {
        msquic_api_->RegistrationClose(msquic_registration_);
        msquic_registration_ = nullptr;
    }
    
    if (msquic_api_) {
        MsQuicClose(msquic_api_);
        msquic_api_ = nullptr;
    }
    
    msquic_initialized_ = false;
    LOG_DEBUG("[QuicServer] msquic 清理完成");
}

QUIC_STATUS QUIC_API QuicServer::ListenerCallback(
    HQUIC Listener,
    void* Context,
    QUIC_LISTENER_EVENT* Event) {
    
    QuicServer* server = static_cast<QuicServer*>(Context);
    
    switch (Event->Type) {
        case QUIC_LISTENER_EVENT_NEW_CONNECTION: {
            // 新连接建立
            HQUIC Connection = Event->NEW_CONNECTION.Connection;
            
            // 设置连接回调
            server->msquic_api_->SetCallbackHandler(Connection, (void*)ConnectionCallback, server);
            
            // 开始接受连接
            server->msquic_api_->ConnectionSetConfiguration(Connection, server->msquic_configuration_);
            
            LOG_DEBUG("[QuicServer] 新连接建立");
            break;
        }
        default:
            break;
    }
    
    return QUIC_STATUS_SUCCESS;
}

QUIC_STATUS QUIC_API QuicServer::ConnectionCallback(
    HQUIC Connection,
    void* Context,
    QUIC_CONNECTION_EVENT* Event) {
    
    QuicServer* server = static_cast<QuicServer*>(Context);
    
    switch (Event->Type) {
        case QUIC_CONNECTION_EVENT_CONNECTED:
            LOG_DEBUG("[QuicServer] 连接已建立");
            break;
            
        case QUIC_CONNECTION_EVENT_SHUTDOWN_INITIATED_BY_TRANSPORT:
        case QUIC_CONNECTION_EVENT_SHUTDOWN_INITIATED_BY_PEER:
            LOG_DEBUG("[QuicServer] 连接关闭");
            break;
            
        case QUIC_CONNECTION_EVENT_PEER_STREAM_STARTED: {
            // 新流开始
            HQUIC Stream = Event->PEER_STREAM_STARTED.Stream;
            
            // 设置流回调
            server->msquic_api_->SetCallbackHandler(Stream, (void*)StreamCallback, server);
            
            // 开始接收数据
            server->msquic_api_->StreamStart(Stream, QUIC_STREAM_START_FLAG_NONE);
            
            LOG_DEBUG("[QuicServer] 新流开始");
            break;
        }
        default:
            break;
    }
    
    return QUIC_STATUS_SUCCESS;
}

QUIC_STATUS QUIC_API QuicServer::StreamCallback(
    HQUIC Stream,
    void* Context,
    QUIC_STREAM_EVENT* Event) {
    
    QuicServer* server = static_cast<QuicServer*>(Context);
    
    switch (Event->Type) {
        case QUIC_STREAM_EVENT_RECEIVE: {
            // 接收数据
            for (uint32_t i = 0; i < Event->RECEIVE.BufferCount; ++i) {
                QUIC_BUFFER* Buffer = &Event->RECEIVE.Buffers[i];
                
                // 查找对应的连接并调用回调
                // TODO: 需要维护 Stream -> Connection 的映射
                std::lock_guard<std::mutex> lock(server->connections_mutex_);
                // 这里应该根据 Stream 找到对应的 QuicConnection 并调用回调
                // 简化实现：直接调用全局回调
                if (server->connection_callback_) {
                    // 创建临时连接对象（实际应该从映射中获取）
                    // 这里需要重新设计连接管理
                }
            }
            
            // 标记接收完成
            server->msquic_api_->StreamReceiveComplete(Stream, Event->RECEIVE.TotalBufferLength);
            break;
        }
        
        case QUIC_STREAM_EVENT_SEND_COMPLETE:
            LOG_DEBUG("[QuicServer] 数据发送完成");
            break;
            
        case QUIC_STREAM_EVENT_SHUTDOWN_COMPLETE:
            LOG_DEBUG("[QuicServer] 流关闭完成");
            break;
            
        default:
            break;
    }
    
    return QUIC_STATUS_SUCCESS;
}
#endif

} // namespace quic
} // namespace gateway

