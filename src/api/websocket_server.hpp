#ifndef WEBSOCKET_SERVER_HPP
#define WEBSOCKET_SERVER_HPP

#define ASIO_STANDALONE

// 禁用第三方库的警告
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wpragmas"
#pragma GCC diagnostic ignored "-Wnull-pointer-subtraction"
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
#include <websocketpp/config/asio_no_tls.hpp>
#include <websocketpp/extensions/permessage_deflate/enabled.hpp>
#include <websocketpp/server.hpp>
#pragma GCC diagnostic pop

#include <functional>
#include <memory>
#include <mutex>
#include <nlohmann/json.hpp>
#include <set>
#include <string>
#include <thread>

namespace api {

class WebSocketServer {
public:
  WebSocketServer(int port = 8089);
  ~WebSocketServer();

  bool Start();
  void Stop();

  // 广播流状态更新
  void BroadcastStreamUpdate(const nlohmann::json &stream_data);

  // 广播所有流列表
  void BroadcastStreamList(const nlohmann::json &streams_data);

  // 广播设备更新（添加/移除）
  void BroadcastDeviceUpdate(const nlohmann::json &device_data);

private:
  // 定义 WebSocket 服务器类型
  // Custom config to enable compression
  struct compression_config : public websocketpp::config::asio {
    // Enable permessage_deflate extension
    struct permessage_deflate_config {
      typedef compression_config::request_type request_type;

      static const bool allow_disabling_context_takeover = true;
      static const uint8_t minimum_outgoing_window_bits = 8;
    };

    typedef websocketpp::extensions::permessage_deflate::enabled<
        permessage_deflate_config>
        permessage_deflate_type;
  };

  // 定义 WebSocket 服务器类型 (using custom config)
  typedef websocketpp::server<compression_config> server;
  typedef server::message_ptr message_ptr;

  // WebSocket 回调
  void on_open(websocketpp::connection_hdl hdl);
  void on_close(websocketpp::connection_hdl hdl);
  void on_message(websocketpp::connection_hdl hdl, message_ptr msg);

  // 内部广播方法
  void BroadcastMessage(const std::string &message);

  // 检查并清理端口（如果是非 WebSocket 服务器占用）

  int port_;
  server ws_server_;
  std::thread ws_thread_;

  // 存储所有WebSocket连接
  // 使用 owner_less 来正确比较 connection_hdl
  std::set<websocketpp::connection_hdl,
           std::owner_less<websocketpp::connection_hdl>>
      connections_;
  std::mutex connections_mutex_;
};

} // namespace api

#endif // WEBSOCKET_SERVER_HPP
