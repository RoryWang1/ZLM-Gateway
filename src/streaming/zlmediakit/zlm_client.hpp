#ifndef STREAMING_ZLMEDIAKIT_ZLM_CLIENT_HPP
#define STREAMING_ZLMEDIAKIT_ZLM_CLIENT_HPP

#include <string>
#include <vector>
#include <memory>
#include <mutex>
#include <curl/curl.h>
#include <nlohmann/json.hpp>

namespace streaming {

/**
 * @brief 流信息
 */
struct StreamInfo {
    std::string app;
    std::string stream;
    std::string schema;
    std::string vhost;
    std::string ip;
    int port = 0;
    bool alive = false;
    int reader_count = 0;
    int total_reader_count = 0;
    int origin_type = 0;
    int origin_type_str = 0;
    int create_stamp = 0;
    int alive_second = 0;
    int bytes_speed = 0;
    int bytes_speed_str = 0;
    int total_bytes = 0;
    int total_bytes_str = 0;
    std::string origin_url;
};

/**
 * @brief RTP 服务器信息（用于GB28181）
 */
struct RtpServerInfo {
    std::string stream_id;      // 流ID（设备ID或通道ID）
    int port = 0;               // RTP接收端口
    int tcp_mode = 0;           // 0=UDP, 1=TCP被动, 2=TCP主动
    bool enable_rtcp = false;   // 是否启用RTCP
    std::string local_ip;       // 本地IP地址
    std::string ssrc;           // SSRC（可选）
};

/**
 * @brief RTP 连接信息
 */
struct RtpInfo {
    std::string peer_ip;        // 对端IP地址
    int peer_port = 0;          // 对端端口
    std::string local_ip;       // 本地IP地址
    int local_port = 0;         // 本地端口
};

/**
 * @brief ZLMediaKit API 客户端
 */
class ZLMClient {
public:
    /**
     * @brief 构造函数
     * @param api_url ZLMediaKit API 地址（如: http://localhost:80）
     * @param secret API 密钥
     */
    ZLMClient(const std::string& api_url, const std::string& secret = "");

    /**
     * @brief 析构函数
     */
    ~ZLMClient();

    /**
     * @brief 添加 RTMP 推流
     * @param app 应用名
     * @param stream 流名
     * @param url 源 URL
     * @return 是否成功
     */
    bool AddRTMPStream(const std::string& app,
                      const std::string& stream,
                      const std::string& url);

    /**
     * @brief 添加 RTSP 推流
     * @param app 应用名
     * @param stream 流名
     * @param url 源 URL
     * @return 是否成功
     */
    bool AddRTSPStream(const std::string& app,
                      const std::string& stream,
                      const std::string& url);

    /**
     * @brief 添加流代理（通用方法，支持所有 ZLMediaKit 原生协议）
     * @param app 应用名
     * @param stream 流名
     * @param url 源 URL（ZLMediaKit 会根据 URL 自动识别协议类型）
     * @return 是否成功
     * 
     * 支持的协议：RTSP, RTMP, HTTP-FLV, HLS 等 ZLMediaKit 原生支持的协议
     */
    bool AddStreamProxy(const std::string& app,
                       const std::string& stream,
                       const std::string& url);

    /**
     * @brief 删除流
     * @param app 应用名
     * @param stream 流名
     * @return 是否成功
     */
    bool DeleteStream(const std::string& app, const std::string& stream);

    /**
     * @brief 获取流列表
     * @param schema 协议类型（可选，空字符串表示所有协议）
     * @return 流信息列表
     */
    std::vector<StreamInfo> GetStreamList(const std::string& schema = "");

    /**
     * @brief 获取流信息
     * @param app 应用名
     * @param stream 流名
     * @param schema 协议类型（可选，空字符串表示所有协议）
     * @return 流信息，不存在返回空对象
     */
    StreamInfo GetStreamInfo(const std::string& app, const std::string& stream, const std::string& schema = "");

    /**
     * @brief 检查服务器是否在线
     * @return 是否在线
     */
    bool IsOnline();

    /**
     * @brief 检查流是否活跃（统一判断逻辑）
     * @param stream_info 流信息
     * @return 是否活跃
     * 
     * 判断标准：
     * 1. 流存在（app不为空）
     * 2. 且满足以下任一条件：
     *    - alive 字段为 true
     *    - bytes_speed > 0（有数据传输）
     *    - total_bytes > 0（有累计数据）
     * 
     * 注意：ZLMediaKit 的 alive 字段可能不准确，需要同时检查数据传输
     */
    static bool IsStreamActive(const StreamInfo& stream_info);

    // ========== GB28181 RTP 服务器 API ==========

    /**
     * @brief 创建GB28181 RTP服务器
     * @param stream_id 流ID（设备ID或通道ID）
     * @param port RTP接收端口（0表示自动分配）
     * @param tcp_mode TCP模式（0=UDP, 1=TCP被动, 2=TCP主动）
     * @param enable_rtcp 是否启用RTCP
     * @param local_ip 本地IP地址（可选，空字符串表示自动选择）
     * @param ssrc SSRC（可选，空字符串表示自动生成）
     * @return RTP服务器信息，失败时port为0
     */
    RtpServerInfo OpenRtpServer(
        const std::string& stream_id,
        uint16_t port = 0,
        int tcp_mode = 0,
        bool enable_rtcp = false,
        const std::string& local_ip = "",
        const std::string& ssrc = "",
        const std::string& app = "gb28181"  // 默认使用gb28181，但允许指定其他app
    );

    /**
     * @brief 关闭GB28181 RTP服务器
     * @param stream_id 流ID
     * @param app 应用名（默认"rtp"）
     * @param vhost 虚拟主机（默认"__defaultVhost__"）
     * @return 是否成功
     */
    bool CloseRtpServer(
        const std::string& stream_id,
        const std::string& app = "rtp",
        const std::string& vhost = "__defaultVhost__"
    );

    /**
     * @brief 获取RTP服务器列表
     * @return RTP服务器信息列表
     */
    std::vector<RtpServerInfo> ListRtpServer();

    /**
     * @brief 获取特定RTP服务器信息
     * @param stream_id 流ID
     * @param app 应用名（默认"rtp"）
     * @param vhost 虚拟主机（默认"__defaultVhost__"）
     * @return RTP服务器信息，不存在时port为0
     */
    RtpServerInfo GetRtpServerInfo(
        const std::string& stream_id,
        const std::string& app = "rtp",
        const std::string& vhost = "__defaultVhost__"
    );

    /**
     * @brief 获取RTP连接信息
     * @param stream_id 流ID
     * @param app 应用名（默认"rtp"）
     * @param vhost 虚拟主机（默认"__defaultVhost__"）
     * @return RTP连接信息，不存在时port为0
     */
    RtpInfo GetRtpInfo(
        const std::string& stream_id,
        const std::string& app = "rtp",
        const std::string& vhost = "__defaultVhost__"
    );

    /**
     * @brief 更新RTP服务器SSRC
     * @param stream_id 流ID
     * @param ssrc 新的SSRC
     * @param app 应用名（默认"rtp"）
     * @param vhost 虚拟主机（默认"__defaultVhost__"）
     * @return 是否成功
     */
    bool UpdateRtpServerSSRC(
        const std::string& stream_id,
        const std::string& ssrc,
        const std::string& app = "rtp",
        const std::string& vhost = "__defaultVhost__"
    );

private:
    /**
     * @brief 执行 API 请求（GET方法）
     * @param api_path API 路径（如: /index/api/getMediaList）
     * @param params 请求参数
     * @return JSON 响应
     */
    nlohmann::json Request(const std::string& api_path,
                          const nlohmann::json& params = nlohmann::json::object());

    /**
     * @brief 执行 API 请求（POST方法）
     * @param api_path API 路径（如: /index/api/openRtpServer）
     * @param params 请求参数（JSON body）
     * @return JSON 响应
     */
    nlohmann::json PostRequest(const std::string& api_path,
                          const nlohmann::json& params = nlohmann::json::object());

    /**
     * @brief 从 JSON 解析流信息
     */
    StreamInfo ParseStreamInfo(const nlohmann::json& j);

    std::string api_url_;
    std::string secret_;
    // 注意：CURL 实例现在在每次请求时临时创建和销毁，无需成员变量
};

} // namespace streaming

#endif // STREAMING_ZLMEDIAKIT_ZLM_CLIENT_HPP

