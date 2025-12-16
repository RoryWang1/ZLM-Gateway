#ifndef GATEWAY_UTILS_ERROR_INFERENCE_HPP
#define GATEWAY_UTILS_ERROR_INFERENCE_HPP

#include "gateway/utils/error_codes.hpp"
#include <string>
#include <algorithm>
#include <cctype>

namespace gateway {
namespace utils {

/**
 * @brief 根据错误消息推断错误码
 * 
 * 分析错误消息内容，推断最可能的错误类型
 * 
 * @param error_message 错误消息（可能来自 ZLM API、FFmpeg 日志等）
 * @return std::pair<error_code, error_message> 推断的错误码和错误消息
 */
inline std::pair<std::string, std::string> InferErrorFromMessage(const std::string& error_message) {
    if (error_message.empty()) {
        return {ErrorCode::UNKNOWN_ERROR, ErrorMessageMapper::GetMessage(ErrorCode::UNKNOWN_ERROR)};
    }
    
    std::string lower_msg = error_message;
    std::transform(lower_msg.begin(), lower_msg.end(), lower_msg.begin(), ::tolower);
    
    // 注意：检查顺序很重要，更具体的错误类型应该先检查
    
    // 连接被拒绝（具体错误，应该在 SOURCE_NOT_FOUND 之前检查）
    if (lower_msg.find("connection refused") != std::string::npos ||
        lower_msg.find("连接被拒绝") != std::string::npos) {
        return {ErrorCode::CONNECTION_REFUSED, ErrorMessageMapper::GetMessage(ErrorCode::CONNECTION_REFUSED)};
    }
    
    // 连接重置（网络错误，应该在 SOURCE_NOT_FOUND 之前检查）
    if (lower_msg.find("connection reset") != std::string::npos ||
        lower_msg.find("连接重置") != std::string::npos) {
        return {ErrorCode::NETWORK_ERROR, ErrorMessageMapper::GetMessage(ErrorCode::NETWORK_ERROR)};
    }
    
    // 源流连接超时
    if (lower_msg.find("timeout") != std::string::npos ||
        lower_msg.find("超时") != std::string::npos ||
        lower_msg.find("timed out") != std::string::npos ||
        lower_msg.find("连接超时") != std::string::npos) {
        return {ErrorCode::SOURCE_TIMEOUT, ErrorMessageMapper::GetMessage(ErrorCode::SOURCE_TIMEOUT)};
    }
    
    // 源流认证失败
    if (lower_msg.find("401") != std::string::npos ||
        lower_msg.find("unauthorized") != std::string::npos ||
        lower_msg.find("认证失败") != std::string::npos ||
        lower_msg.find("authentication failed") != std::string::npos ||
        lower_msg.find("forbidden") != std::string::npos ||
        lower_msg.find("403") != std::string::npos) {
        return {ErrorCode::SOURCE_AUTH_FAILED, ErrorMessageMapper::GetMessage(ErrorCode::SOURCE_AUTH_FAILED)};
    }
    
    // 网络错误（通用，应该在 SOURCE_NOT_FOUND 之前检查，但要在具体网络错误之后）
    if (lower_msg.find("network") != std::string::npos ||
        lower_msg.find("网络") != std::string::npos ||
        lower_msg.find("socket") != std::string::npos) {
        return {ErrorCode::NETWORK_ERROR, ErrorMessageMapper::GetMessage(ErrorCode::NETWORK_ERROR)};
    }
    
    // 源流不存在或无法访问（通用错误，应该最后检查）
    if (lower_msg.find("no route to host") != std::string::npos ||
        lower_msg.find("无法访问") != std::string::npos ||
        lower_msg.find("stream not found") != std::string::npos ||
        lower_msg.find("netstream.play.streamnotfound") != std::string::npos ||
        lower_msg.find("404") != std::string::npos ||
        lower_msg.find("not found") != std::string::npos ||
        lower_msg.find("不存在") != std::string::npos ||
        lower_msg.find("connection") != std::string::npos ||
        lower_msg.find("连接") != std::string::npos) {
        return {ErrorCode::SOURCE_NOT_FOUND, ErrorMessageMapper::GetMessage(ErrorCode::SOURCE_NOT_FOUND)};
    }
    
    // 编解码不支持
    if (lower_msg.find("codec") != std::string::npos ||
        lower_msg.find("编码") != std::string::npos ||
        lower_msg.find("unsupported") != std::string::npos ||
        lower_msg.find("不支持") != std::string::npos) {
        return {ErrorCode::CODEC_UNSUPPORTED, ErrorMessageMapper::GetMessage(ErrorCode::CODEC_UNSUPPORTED)};
    }
    
    // 默认返回流验证失败
    return {ErrorCode::STREAM_VALIDATION_FAILED, ErrorMessageMapper::GetMessage(ErrorCode::STREAM_VALIDATION_FAILED)};
}

/**
 * @brief 根据错误日志推断错误码
 * 
 * 分析错误日志内容（通常来自 FFmpeg 日志文件），推断最可能的错误类型
 * 此函数与 InferErrorFromMessage 逻辑相同，只是语义上更明确表示处理的是日志内容
 * 
 * @param error_log 错误日志内容（通常来自 FFmpeg 日志文件）
 * @return std::pair<error_code, error_message> 推断的错误码和错误消息
 */
inline std::pair<std::string, std::string> InferErrorFromLog(const std::string& error_log) {
    // 直接使用 InferErrorFromMessage，因为逻辑完全相同
    return InferErrorFromMessage(error_log);
}

} // namespace utils
} // namespace gateway

#endif // GATEWAY_UTILS_ERROR_INFERENCE_HPP

