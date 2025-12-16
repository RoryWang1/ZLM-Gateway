// 禁用第三方库的警告
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wnull-pointer-subtraction"
#include "api/websocket_server.hpp"
#pragma GCC diagnostic pop
#include "config/constants.hpp"
#include "utils/logger.hpp"
#include "utils/system_utils.hpp"
#include <iostream>
#include <sstream>
#include <cstdlib>
#include <thread>
#include <chrono>

using namespace config::constants;

namespace api {

WebSocketServer::WebSocketServer(int port) : port_(port) {
    // 初始化 Asio
    ws_server_.init_asio();
    
    // 设置日志级别 (只记录错误)
    ws_server_.set_access_channels(websocketpp::log::alevel::none);
    ws_server_.set_error_channels(websocketpp::log::elevel::all);
    
    // 注册回调函数
    ws_server_.set_open_handler(bind(&WebSocketServer::on_open, this, std::placeholders::_1));
    ws_server_.set_close_handler(bind(&WebSocketServer::on_close, this, std::placeholders::_1));
    ws_server_.set_message_handler(bind(&WebSocketServer::on_message, this, std::placeholders::_1, std::placeholders::_2));
    
    // 设置验证处理器，接受所有路径的连接（包括 /ws/streams）
    ws_server_.set_validate_handler([](websocketpp::connection_hdl) {
        return true;  // 接受所有连接
    });
    
    // 允许地址重用
    ws_server_.set_reuse_addr(true);
    
    LOG_INFO("WebSocket服务器初始化，端口: {}", port_);
}

WebSocketServer::~WebSocketServer() {
    Stop();
}

bool WebSocketServer::Start() {
    try {
        // 检查端口是否被占用，如果是非 WebSocket 服务器占用，尝试清理
        ::utils::SystemUtils::CheckAndCleanPort(port_);
        
        // 监听端口
        ws_server_.listen(port_);
        
        // 开始接受连接
        ws_server_.start_accept();
        
        // 在后台线程运行
        ws_thread_ = std::thread([this]() {
            try {
                LOG_INFO("启动WebSocket服务器，端口: {}", port_);
                ws_server_.run();
            } catch (const std::exception& e) {
                std::string error_msg = e.what();
                // 检查是否是端口占用错误
                if (error_msg.find("Address already in use") != std::string::npos ||
                    error_msg.find("address already in use") != std::string::npos ||
                    error_msg.find("asio.system:48") != std::string::npos) {
                    ::utils::Logger::Get()->warn("WebSocket端口 {} 已被占用，可能已有实例在运行", port_);
                } else {
                    LOG_ERROR("WebSocket服务器运行时错误: {}", e.what());
                }
            } catch (...) {
                LOG_ERROR("WebSocket服务器运行时未知错误");
            }
        });
        
        LOG_INFO("WebSocket服务器已启动: ws://0.0.0.0:{}", port_);
        return true;
    } catch (const std::exception& e) {
        std::string error_msg = e.what();
        // 检查是否是端口占用错误
        if (error_msg.find("Address already in use") != std::string::npos ||
            error_msg.find("address already in use") != std::string::npos ||
            error_msg.find("asio.system:48") != std::string::npos) {
            ::utils::Logger::Get()->warn("WebSocket端口 {} 已被占用，可能已有实例在运行，跳过启动", port_);
        } else {
            LOG_ERROR("WebSocket服务器启动失败: {}", e.what());
        }
        return false;
    }
}

void WebSocketServer::Stop() {
    if (ws_server_.stopped()) {
        return;
    }
    
    LOG_INFO("停止WebSocket服务器");
    
    // 停止服务器
    ws_server_.stop();
    
    // 等待线程结束
    if (ws_thread_.joinable()) {
        ws_thread_.join();
    }
}

void WebSocketServer::on_open(websocketpp::connection_hdl hdl) {
    std::lock_guard<std::mutex> lock(connections_mutex_);
    connections_.insert(hdl);
    LOG_INFO("新的WebSocket连接，当前连接数: {}", connections_.size());
}

void WebSocketServer::on_close(websocketpp::connection_hdl hdl) {
    std::lock_guard<std::mutex> lock(connections_mutex_);
    connections_.erase(hdl);
    LOG_INFO("WebSocket连接断开，当前连接数: {}", connections_.size());
}

void WebSocketServer::on_message(websocketpp::connection_hdl /* hdl */, message_ptr msg) {
    // 暂时不需要处理客户端消息，但记录一下
    LOG_DEBUG("收到WebSocket消息: {}", msg->get_payload());
}

void WebSocketServer::BroadcastStreamUpdate(const nlohmann::json& stream_data) {
    nlohmann::json message = {
        {"type", "stream_update"},
        {"data", stream_data}
    };
    
    BroadcastMessage(message.dump());
}

void WebSocketServer::BroadcastStreamList(const nlohmann::json& streams_data) {
    nlohmann::json message = {
        {"type", "stream_list"},
        {"data", streams_data}
    };
    
    BroadcastMessage(message.dump());
}

void WebSocketServer::BroadcastDeviceUpdate(const nlohmann::json& device_data) {
    nlohmann::json message = {
        {"type", "device_update"},
        {"data", device_data}
    };
    
    BroadcastMessage(message.dump());
}

void WebSocketServer::BroadcastMessage(const std::string& message) {
    std::lock_guard<std::mutex> lock(connections_mutex_);
    
    if (!connections_.empty()) {
        LOG_INFO("Broadcasting message to {} clients: {}", connections_.size(), message);
    }

    for (auto it = connections_.begin(); it != connections_.end(); ) {
        try {
            ws_server_.send(*it, message, websocketpp::frame::opcode::text);
            ++it;
        } catch (const websocketpp::exception& e) {
            LOG_WARN("发送WebSocket消息失败: {}", e.what());
            // 如果发送失败，认为是连接已断开，移除它
            it = connections_.erase(it);
        }
    }
}



} // namespace api
