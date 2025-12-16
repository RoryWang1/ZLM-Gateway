#ifndef GATEWAY_GB28181_SIP_MESSAGE_HPP
#define GATEWAY_GB28181_SIP_MESSAGE_HPP

#include <string>
#include <map>
#include <vector>

namespace gateway {

/**
 * @brief SIP 消息类型
 */
enum class SipMethod {
    REGISTER,
    INVITE,
    BYE,
    MESSAGE,
    ACK,
    CANCEL,
    UNKNOWN
};

/**
 * @brief SIP 消息（请求或响应）
 */
struct SipMessage {
    // 请求行/状态行
    SipMethod method = SipMethod::UNKNOWN;
    std::string uri;              // 请求URI（请求消息）
    int status_code = 0;          // 状态码（响应消息）
    std::string reason_phrase;    // 原因短语（响应消息）
    
    // 头部字段
    std::map<std::string, std::string> headers;
    
    // 消息体（SDP等）
    std::string body;
    
    // 原始消息
    std::string raw_message;
    
    // 解析信息
    std::string call_id;
    std::string from;
    std::string to;
    std::string via;
    std::string contact;
    int cseq = 0;
    std::string cseq_method;
    int expires = 0;
    
    /**
     * @brief 获取头部字段值
     */
    std::string GetHeader(const std::string& name) const {
        auto it = headers.find(name);
        if (it != headers.end()) {
            return it->second;
        }
        // 尝试不区分大小写查找
        for (const auto& pair : headers) {
            std::string lower_key = pair.first;
            std::transform(lower_key.begin(), lower_key.end(), lower_key.begin(), ::tolower);
            std::string lower_name = name;
            std::transform(lower_name.begin(), lower_name.end(), lower_name.begin(), ::tolower);
            if (lower_key == lower_name) {
                return pair.second;
            }
        }
        return "";
    }
    
    /**
     * @brief 设置头部字段
     */
    void SetHeader(const std::string& name, const std::string& value) {
        headers[name] = value;
    }
    
    /**
     * @brief 检查是否是请求消息
     */
    bool IsRequest() const {
        return method != SipMethod::UNKNOWN && status_code == 0;
    }
    
    /**
     * @brief 检查是否是响应消息
     */
    bool IsResponse() const {
        return status_code > 0;
    }
};

/**
 * @brief SIP 消息解析器
 */
class SipMessageParser {
public:
    /**
     * @brief 解析SIP消息
     * @param data 原始消息数据
     * @param len 数据长度
     * @return 解析后的SIP消息，失败返回空对象
     */
    static SipMessage Parse(const char* data, size_t len);
    
    /**
     * @brief 解析SIP消息（字符串版本）
     */
    static SipMessage Parse(const std::string& message);
    
    /**
     * @brief 构造SIP响应消息
     * @param request 原始请求
     * @param status_code 状态码
     * @param reason_phrase 原因短语
     * @param headers 额外的头部字段
     * @param body 消息体（可选）
     * @return 构造的响应消息
     */
    static std::string BuildResponse(
        const SipMessage& request,
        int status_code,
        const std::string& reason_phrase,
        const std::map<std::string, std::string>& extra_headers = {},
        const std::string& body = ""
    );
    
    /**
     * @brief 构造SIP请求消息
     */
    static std::string BuildRequest(
        SipMethod method,
        const std::string& uri,
        const std::string& from,
        const std::string& to,
        const std::string& call_id,
        int cseq,
        const std::map<std::string, std::string>& extra_headers = {},
        const std::string& body = ""
    );
    
    /**
     * @brief 解析From/To头部，提取设备ID
     */
    static std::string ExtractDeviceId(const std::string& from_to_header);
    
    /**
     * @brief 解析Contact头部，提取IP和端口
     */
    static bool ExtractContactInfo(const std::string& contact_header, std::string& ip, int& port);
    
    /**
     * @brief 解析Via头部，提取IP和端口
     */
    static bool ExtractViaInfo(const std::string& via_header, std::string& ip, int& port);
    
    /**
     * @brief 字符串转SipMethod
     */
    static SipMethod StringToMethod(const std::string& method_str);
    
    /**
     * @brief SipMethod转字符串
     */
    static std::string MethodToString(SipMethod method);
    
    /**
     * @brief 解析Authorization头部（Digest Authentication）
     */
    static bool ParseAuthorizationHeader(
        const std::string& auth_header,
        std::string& username,
        std::string& realm,
        std::string& nonce,
        std::string& uri,
        std::string& response,
        std::string& algorithm);
    
    /**
     * @brief 计算MD5哈希值
     */
    static std::string CalculateMD5(const std::string& input);
    
    /**
     * @brief 验证Digest Authentication响应
     */
    static bool VerifyDigestAuth(
        const std::string& username,
        const std::string& password,
        const std::string& realm,
        const std::string& method,
        const std::string& uri,
        const std::string& nonce,
        const std::string& response);

private:
    /**
     * @brief 解析请求行
     */
    static bool ParseRequestLine(const std::string& line, SipMessage& msg);
    
    /**
     * @brief 解析状态行
     */
    static bool ParseStatusLine(const std::string& line, SipMessage& msg);
    
    /**
     * @brief 解析头部字段
     */
    static void ParseHeaders(const std::vector<std::string>& lines, SipMessage& msg);
    
    /**
     * @brief 分割字符串
     */
    static std::vector<std::string> Split(const std::string& str, char delimiter);
    
    /**
     * @brief 去除首尾空白
     */
    static std::string Trim(const std::string& str);
    
    /**
     * @brief URL解码
     */
    static std::string UrlDecode(const std::string& str);
};

} // namespace gateway

#endif // GATEWAY_GB28181_SIP_MESSAGE_HPP

