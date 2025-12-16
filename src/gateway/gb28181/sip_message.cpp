#include "sip_message.hpp"
#include "utils/logger.hpp"
#include <sstream>
#include <algorithm>
#include <cctype>
#include <iomanip>
#include <chrono>
#include <openssl/md5.h>
#include <openssl/evp.h>
#include <cstring>

namespace gateway {

SipMessage SipMessageParser::Parse(const char* data, size_t len) {
    return Parse(std::string(data, len));
}

SipMessage SipMessageParser::Parse(const std::string& message) {
    SipMessage msg;
    msg.raw_message = message;
    
    if (message.empty()) {
        return msg;
    }
    
    // 按行分割
    std::vector<std::string> lines = Split(message, '\n');
    if (lines.empty()) {
        return msg;
    }
    
    // 解析第一行（请求行或状态行）
    std::string first_line = Trim(lines[0]);
    if (first_line.empty() && lines.size() > 1) {
        first_line = Trim(lines[1]);
    }
    
    // 判断是请求还是响应：响应以"SIP/2.0"开头，请求以方法名开头（如REGISTER, INVITE, MESSAGE等）
    if (first_line.find("SIP/2.0") == 0) {
        // 状态行（响应）：格式为 "SIP/2.0 <status_code> <reason>"
        if (!ParseStatusLine(first_line, msg)) {
            LOG_ERROR("解析SIP状态行失败: {}", first_line);
            return msg;
        }
    } else {
        // 请求行：格式为 "<METHOD> <URI> SIP/2.0"
        if (!ParseRequestLine(first_line, msg)) {
            LOG_ERROR("解析SIP请求行失败: {}", first_line);
            return msg;
        }
    }
    
    // 解析头部字段
    std::vector<std::string> header_lines;
    size_t body_start = 0;
    for (size_t i = 1; i < lines.size(); ++i) {
        std::string line = lines[i];
        if (line.empty() || (line == "\r" && i + 1 < lines.size())) {
            body_start = i + 1;
            break;
        }
        header_lines.push_back(line);
    }
    
    ParseHeaders(header_lines, msg);
    
    // 解析消息体
    if (body_start > 0 && body_start < lines.size()) {
        std::ostringstream body_stream;
        for (size_t i = body_start; i < lines.size(); ++i) {
            if (i > body_start) {
                body_stream << "\n";
            }
            body_stream << lines[i];
        }
        msg.body = body_stream.str();
    }
    
    return msg;
}

bool SipMessageParser::ParseRequestLine(const std::string& line, SipMessage& msg) {
    std::vector<std::string> parts = Split(line, ' ');
    if (parts.size() < 3) {
        return false;
    }
    
    msg.method = StringToMethod(parts[0]);
    msg.uri = parts[1];
    // parts[2] 应该是 "SIP/2.0"
    
    return true;
}

bool SipMessageParser::ParseStatusLine(const std::string& line, SipMessage& msg) {
    std::vector<std::string> parts = Split(line, ' ');
    if (parts.size() < 3) {
        return false;
    }
    
    // parts[0] 应该是 "SIP/2.0"
    try {
        msg.status_code = std::stoi(parts[1]);
    } catch (...) {
        return false;
    }
    
    // 原因短语
    if (parts.size() > 2) {
        msg.reason_phrase = parts[2];
        for (size_t i = 3; i < parts.size(); ++i) {
            msg.reason_phrase += " " + parts[i];
        }
    }
    
    return true;
}

void SipMessageParser::ParseHeaders(const std::vector<std::string>& lines, SipMessage& msg) {
    std::string current_header;
    std::string current_value;
    
    for (const auto& line : lines) {
        std::string trimmed = Trim(line);
        if (trimmed.empty()) {
            continue;
        }
        
        // 检查是否是续行（以空格或TAB开头）
        if (trimmed[0] == ' ' || trimmed[0] == '\t') {
            // 续行，追加到当前值
            if (!current_header.empty()) {
                current_value += " " + Trim(trimmed);
            }
        } else {
            // 新头部字段
            if (!current_header.empty()) {
                // 保存之前的头部字段
                msg.headers[current_header] = current_value;
                msg.SetHeader(current_header, current_value);
            }
            
            // 解析新的头部字段
            size_t colon_pos = trimmed.find(':');
            if (colon_pos != std::string::npos) {
                current_header = Trim(trimmed.substr(0, colon_pos));
                current_value = Trim(trimmed.substr(colon_pos + 1));
            } else {
                current_header = trimmed;
                current_value = "";
            }
        }
    }
    
    // 保存最后一个头部字段
    if (!current_header.empty()) {
        msg.headers[current_header] = current_value;
        msg.SetHeader(current_header, current_value);
    }
    
    // 提取常用字段
    msg.call_id = msg.GetHeader("Call-ID");
    msg.from = msg.GetHeader("From");
    msg.to = msg.GetHeader("To");
    msg.via = msg.GetHeader("Via");
    msg.contact = msg.GetHeader("Contact");
    
    // 解析CSeq
    std::string cseq = msg.GetHeader("CSeq");
    if (!cseq.empty()) {
        std::vector<std::string> parts = Split(cseq, ' ');
        if (!parts.empty()) {
            try {
                msg.cseq = std::stoi(parts[0]);
                if (parts.size() > 1) {
                    msg.cseq_method = parts[1];
                }
            } catch (...) {
                // 忽略解析错误
            }
        }
    }
    
    // 解析Expires
    std::string expires = msg.GetHeader("Expires");
    if (!expires.empty()) {
        try {
            msg.expires = std::stoi(expires);
        } catch (...) {
            // 忽略解析错误
        }
    }
}

std::string SipMessageParser::BuildResponse(
    const SipMessage& request,
    int status_code,
    const std::string& reason_phrase,
    const std::map<std::string, std::string>& extra_headers,
    const std::string& body) {
    
    std::ostringstream response;
    
    // 状态行
    response << "SIP/2.0 " << status_code << " " << reason_phrase << "\r\n";
    
    // Via（从请求中复制）
    if (!request.via.empty()) {
        response << "Via: " << request.via << "\r\n";
    }
    
    // From（从请求中复制）
    if (!request.from.empty()) {
        response << "From: " << request.from << "\r\n";
    }
    
    // To（从请求中复制）
    if (!request.to.empty()) {
        response << "To: " << request.to;
        // 如果状态码不是100 Trying，添加tag
        if (status_code != 100) {
            // 检查To是否已有tag
            if (request.to.find("tag=") == std::string::npos) {
                response << ";tag=" << std::to_string(std::chrono::system_clock::now().time_since_epoch().count());
            }
        }
        response << "\r\n";
    }
    
    // Call-ID（从请求中复制）
    if (!request.call_id.empty()) {
        response << "Call-ID: " << request.call_id << "\r\n";
    }
    
    // CSeq（从请求中复制）
    if (request.cseq > 0 && !request.cseq_method.empty()) {
        response << "CSeq: " << request.cseq << " " << request.cseq_method << "\r\n";
    }
    
    // 额外的头部字段
    for (const auto& pair : extra_headers) {
        response << pair.first << ": " << pair.second << "\r\n";
    }
    
    // Content-Length
    if (!body.empty()) {
        response << "Content-Length: " << body.length() << "\r\n";
    } else {
        response << "Content-Length: 0\r\n";
    }
    
    // 空行
    response << "\r\n";
    
    // 消息体
    if (!body.empty()) {
        response << body;
    }
    
    return response.str();
}

std::string SipMessageParser::BuildRequest(
    SipMethod method,
    const std::string& uri,
    const std::string& from,
    const std::string& to,
    const std::string& call_id,
    int cseq,
    const std::map<std::string, std::string>& extra_headers,
    const std::string& body) {
    
    std::ostringstream request;
    
    // 请求行
    request << MethodToString(method) << " " << uri << " SIP/2.0\r\n";
    
    // From
    request << "From: " << from << "\r\n";
    
    // To
    request << "To: " << to << "\r\n";
    
    // Call-ID
    request << "Call-ID: " << call_id << "\r\n";
    
    // CSeq
    request << "CSeq: " << cseq << " " << MethodToString(method) << "\r\n";
    
    // 额外的头部字段
    for (const auto& pair : extra_headers) {
        request << pair.first << ": " << pair.second << "\r\n";
    }
    
    // Content-Length
    if (!body.empty()) {
        request << "Content-Length: " << body.length() << "\r\n";
    } else {
        request << "Content-Length: 0\r\n";
    }
    
    // 空行
    request << "\r\n";
    
    // 消息体
    if (!body.empty()) {
        request << body;
    }
    
    return request.str();
}

std::string SipMessageParser::ExtractDeviceId(const std::string& from_to_header) {
    // From/To格式: "设备ID" <sip:设备ID@域> 或 设备ID <sip:设备ID@域>
    // 提取sip:后面的设备ID
    size_t sip_pos = from_to_header.find("sip:");
    if (sip_pos != std::string::npos) {
        size_t start = sip_pos + 4;
        size_t at_pos = from_to_header.find('@', start);
        if (at_pos != std::string::npos) {
            return Trim(from_to_header.substr(start, at_pos - start));
        }
    }
    
    // 如果没有找到sip:，尝试提取引号中的内容
    size_t quote_start = from_to_header.find('"');
    if (quote_start != std::string::npos) {
        size_t quote_end = from_to_header.find('"', quote_start + 1);
        if (quote_end != std::string::npos) {
            return Trim(from_to_header.substr(quote_start + 1, quote_end - quote_start - 1));
        }
    }
    
    return "";
}

bool SipMessageParser::ExtractContactInfo(const std::string& contact_header, std::string& ip, int& port) {
    // Contact格式: <sip:设备ID@IP:端口> 或 sip:设备ID@IP:端口
    size_t sip_pos = contact_header.find("sip:");
    if (sip_pos == std::string::npos) {
        return false;
    }
    
    size_t at_pos = contact_header.find('@', sip_pos + 4);
    if (at_pos == std::string::npos) {
        return false;
    }
    
    size_t start = at_pos + 1;
    size_t colon_pos = contact_header.find(':', start);
    size_t end = contact_header.find('>', start);
    if (end == std::string::npos) {
        end = contact_header.find(';', start);
        if (end == std::string::npos) {
            end = contact_header.length();
        }
    }
    
    if (colon_pos != std::string::npos && colon_pos < end) {
        ip = Trim(contact_header.substr(start, colon_pos - start));
        try {
            port = std::stoi(contact_header.substr(colon_pos + 1, end - colon_pos - 1));
        } catch (...) {
            port = 5060;  // 默认端口
        }
    } else {
        ip = Trim(contact_header.substr(start, end - start));
        port = 5060;  // 默认端口
    }
    
    return !ip.empty();
}

bool SipMessageParser::ExtractViaInfo(const std::string& via_header, std::string& ip, int& port) {
    // Via格式: SIP/2.0/UDP IP:端口;branch=...
    size_t space_pos = via_header.find(' ');
    if (space_pos == std::string::npos) {
        return false;
    }
    
    size_t start = space_pos + 1;
    size_t colon_pos = via_header.find(':', start);
    size_t semicolon_pos = via_header.find(';', start);
    
    size_t end = semicolon_pos;
    if (end == std::string::npos) {
        end = via_header.length();
    }
    
    if (colon_pos != std::string::npos && colon_pos < end) {
        ip = Trim(via_header.substr(start, colon_pos - start));
        try {
            port = std::stoi(via_header.substr(colon_pos + 1, end - colon_pos - 1));
        } catch (...) {
            port = 5060;
        }
    } else {
        ip = Trim(via_header.substr(start, end - start));
        port = 5060;
    }
    
    return !ip.empty();
}

SipMethod SipMessageParser::StringToMethod(const std::string& method_str) {
    std::string upper = method_str;
    std::transform(upper.begin(), upper.end(), upper.begin(), ::toupper);
    
    if (upper == "REGISTER") return SipMethod::REGISTER;
    if (upper == "INVITE") return SipMethod::INVITE;
    if (upper == "BYE") return SipMethod::BYE;
    if (upper == "MESSAGE") return SipMethod::MESSAGE;
    if (upper == "ACK") return SipMethod::ACK;
    if (upper == "CANCEL") return SipMethod::CANCEL;
    return SipMethod::UNKNOWN;
}

std::string SipMessageParser::MethodToString(SipMethod method) {
    switch (method) {
        case SipMethod::REGISTER: return "REGISTER";
        case SipMethod::INVITE: return "INVITE";
        case SipMethod::BYE: return "BYE";
        case SipMethod::MESSAGE: return "MESSAGE";
        case SipMethod::ACK: return "ACK";
        case SipMethod::CANCEL: return "CANCEL";
        default: return "UNKNOWN";
    }
}

std::vector<std::string> SipMessageParser::Split(const std::string& str, char delimiter) {
    std::vector<std::string> result;
    std::istringstream stream(str);
    std::string item;
    while (std::getline(stream, item, delimiter)) {
        result.push_back(item);
    }
    return result;
}

std::string SipMessageParser::Trim(const std::string& str) {
    size_t start = str.find_first_not_of(" \t\r\n");
    if (start == std::string::npos) {
        return "";
    }
    size_t end = str.find_last_not_of(" \t\r\n");
    return str.substr(start, end - start + 1);
}

std::string SipMessageParser::UrlDecode(const std::string& str) {
    std::ostringstream decoded;
    for (size_t i = 0; i < str.length(); ++i) {
        if (str[i] == '%' && i + 2 < str.length()) {
            int value;
            std::istringstream hex(str.substr(i + 1, 2));
            if (hex >> std::hex >> value) {
                decoded << static_cast<char>(value);
                i += 2;
            } else {
                decoded << str[i];
            }
        } else if (str[i] == '+') {
            decoded << ' ';
        } else {
            decoded << str[i];
        }
    }
    return decoded.str();
}

std::string SipMessageParser::CalculateMD5(const std::string& input) {
    unsigned char digest[EVP_MD_size(EVP_md5())];
    unsigned int digest_len = 0;
    
    EVP_MD_CTX* ctx = EVP_MD_CTX_new();
    if (!ctx) {
        return "";
    }
    
    if (EVP_DigestInit_ex(ctx, EVP_md5(), nullptr) != 1 ||
        EVP_DigestUpdate(ctx, input.c_str(), input.length()) != 1 ||
        EVP_DigestFinal_ex(ctx, digest, &digest_len) != 1) {
        EVP_MD_CTX_free(ctx);
        return "";
    }
    
    EVP_MD_CTX_free(ctx);
    
    std::ostringstream oss;
    for (unsigned int i = 0; i < digest_len; ++i) {
        oss << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(digest[i]);
    }
    return oss.str();
}

bool SipMessageParser::ParseAuthorizationHeader(
    const std::string& auth_header,
    std::string& username,
    std::string& realm,
    std::string& nonce,
    std::string& uri,
    std::string& response,
    std::string& algorithm) {
    
    // Authorization格式: Digest username="xxx", realm="xxx", nonce="xxx", uri="xxx", response="xxx"
    if (auth_header.find("Digest ") != 0) {
        return false;
    }
    
    std::string digest_part = auth_header.substr(7);  // 跳过 "Digest "
    
    // 解析各个参数
    std::istringstream iss(digest_part);
    std::string token;
    
    while (std::getline(iss, token, ',')) {
        token = Trim(token);
        size_t eq_pos = token.find('=');
        if (eq_pos == std::string::npos) {
            continue;
        }
        
        std::string key = Trim(token.substr(0, eq_pos));
        std::string value = Trim(token.substr(eq_pos + 1));
        
        // 去除引号
        if (value.length() >= 2 && value[0] == '"' && value[value.length() - 1] == '"') {
            value = value.substr(1, value.length() - 2);
        }
        
        if (key == "username") {
            username = value;
        } else if (key == "realm") {
            realm = value;
        } else if (key == "nonce") {
            nonce = value;
        } else if (key == "uri") {
            uri = value;
        } else if (key == "response") {
            response = value;
        } else if (key == "algorithm") {
            algorithm = value;
        }
    }
    
    return !username.empty() && !realm.empty() && !nonce.empty() && !uri.empty() && !response.empty();
}

bool SipMessageParser::VerifyDigestAuth(
    const std::string& username,
    const std::string& password,
    const std::string& realm,
    const std::string& method,
    const std::string& uri,
    const std::string& nonce,
    const std::string& response) {
    
    // 计算HA1 = MD5(username:realm:password)
    std::string ha1_input = username + ":" + realm + ":" + password;
    std::string ha1 = CalculateMD5(ha1_input);
    
    // 计算HA2 = MD5(method:uri)
    std::string ha2_input = method + ":" + uri;
    std::string ha2 = CalculateMD5(ha2_input);
    
    // 计算response = MD5(HA1:nonce:HA2)
    std::string response_input = ha1 + ":" + nonce + ":" + ha2;
    std::string calculated_response = CalculateMD5(response_input);
    
    // 转换为小写比较（MD5哈希值通常是小写）
    std::string response_lower = response;
    std::transform(response_lower.begin(), response_lower.end(), response_lower.begin(), ::tolower);
    std::string calculated_lower = calculated_response;
    std::transform(calculated_lower.begin(), calculated_lower.end(), calculated_lower.begin(), ::tolower);
    
    return response_lower == calculated_lower;
}

} // namespace gateway

