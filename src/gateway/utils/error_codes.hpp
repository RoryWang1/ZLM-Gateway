#ifndef GATEWAY_UTILS_ERROR_CODES_HPP
#define GATEWAY_UTILS_ERROR_CODES_HPP

#include <string>
#include <map>

namespace gateway {
namespace utils {

/**
 * @brief 标准错误码定义
 * 
 * 用于统一错误模型，便于前端展示和运维排查
 */
namespace ErrorCode {
    // 源流相关错误
    constexpr const char* SOURCE_NOT_FOUND = "SOURCE_NOT_FOUND";           // 源流不存在或无法访问
    constexpr const char* SOURCE_TIMEOUT = "SOURCE_TIMEOUT";                // 源流连接超时
    constexpr const char* SOURCE_AUTH_FAILED = "SOURCE_AUTH_FAILED";        // 源流认证失败
    constexpr const char* SOURCE_FORBIDDEN = "SOURCE_FORBIDDEN";            // 源流访问被禁止
    
    // 编解码相关错误
    constexpr const char* CODEC_UNSUPPORTED = "CODEC_UNSUPPORTED";         // 编码格式不支持
    constexpr const char* CODEC_DETECTION_FAILED = "CODEC_DETECTION_FAILED"; // 编解码检测失败
    
    // 网络相关错误
    constexpr const char* NETWORK_ERROR = "NETWORK_ERROR";                  // 网络异常
    constexpr const char* CONNECTION_REFUSED = "CONNECTION_REFUSED";        // 连接被拒绝
    constexpr const char* DNS_RESOLUTION_FAILED = "DNS_RESOLUTION_FAILED";  // DNS 解析失败
    
    // 进程相关错误
    constexpr const char* PROCESS_CRASH = "PROCESS_CRASH";                  // 进程异常退出
    constexpr const char* PROCESS_START_FAILED = "PROCESS_START_FAILED";    // 进程启动失败
    constexpr const char* FFMPEG_COMMAND_EMPTY = "FFMPEG_COMMAND_EMPTY";    // FFmpeg 命令为空
    constexpr const char* FFMPEG_COMMAND_BUILDER_MISSING = "FFMPEG_COMMAND_BUILDER_MISSING"; // FFmpeg 命令构建器缺失
    constexpr const char* FFMPEG_START_FAILED = "FFMPEG_START_FAILED";     // FFmpeg 启动失败
    
    // 流验证相关错误
    constexpr const char* STREAM_VALIDATION_FAILED = "STREAM_VALIDATION_FAILED"; // 流验证失败
    constexpr const char* STREAM_NOT_READY = "STREAM_NOT_READY";            // 流未就绪
    constexpr const char* STREAM_TIMEOUT = "STREAM_TIMEOUT";                 // 流启动超时
    
    // ZLM API 相关错误
    constexpr const char* ZLM_API_FAILED = "ZLM_API_FAILED";                // ZLM API 调用失败
    constexpr const char* ZLM_NOT_AVAILABLE = "ZLM_NOT_AVAILABLE";          // ZLM 服务不可用
    constexpr const char* ZLM_STREAM_NOT_FOUND = "ZLM_STREAM_NOT_FOUND";     // ZLM 中流不存在
    
    // 设备相关错误
    constexpr const char* DEVICE_NOT_FOUND = "DEVICE_NOT_FOUND";            // 设备不存在
    constexpr const char* DEVICE_BUSY = "DEVICE_BUSY";                      // 设备忙碌
    constexpr const char* DEVICE_OFFLINE = "DEVICE_OFFLINE";                // 设备离线
    
    // 配置相关错误
    constexpr const char* INVALID_CONFIG = "INVALID_CONFIG";                 // 配置无效
    constexpr const char* UNSUPPORTED_SOURCE_TYPE = "UNSUPPORTED_SOURCE_TYPE"; // 不支持的源类型
    
    // 其他错误
    constexpr const char* TIMEOUT = "TIMEOUT";                              // 操作超时
    constexpr const char* UNKNOWN_ERROR = "UNKNOWN_ERROR";                  // 未知错误
}

/**
 * @brief 错误消息映射
 * 
 * 将错误码映射到用户友好的错误消息
 */
class ErrorMessageMapper {
public:
    /**
     * @brief 获取错误消息
     * @param error_code 错误码
     * @return 错误消息
     */
    static std::string GetMessage(const std::string& error_code) {
        static const std::map<std::string, std::string> error_messages = {
            // 源流相关
            {ErrorCode::SOURCE_NOT_FOUND, "源流不存在或无法访问，请检查摄像头/上游地址"},
            {ErrorCode::SOURCE_TIMEOUT, "源流连接超时，请检查网络连接"},
            {ErrorCode::SOURCE_AUTH_FAILED, "源流认证失败，请检查用户名和密码"},
            {ErrorCode::SOURCE_FORBIDDEN, "源流访问被禁止，请检查权限配置"},
            
            // 编解码相关
            {ErrorCode::CODEC_UNSUPPORTED, "源流编码格式不支持，请检查音视频编码配置"},
            {ErrorCode::CODEC_DETECTION_FAILED, "无法检测源流编码格式，请检查源流是否正常"},
            
            // 网络相关
            {ErrorCode::NETWORK_ERROR, "网络异常或上游服务不可用"},
            {ErrorCode::CONNECTION_REFUSED, "连接被拒绝，请检查目标地址和端口"},
            {ErrorCode::DNS_RESOLUTION_FAILED, "DNS 解析失败，请检查域名配置"},
            
            // 进程相关
            {ErrorCode::PROCESS_CRASH, "转码进程异常退出，请查看服务日志"},
            {ErrorCode::PROCESS_START_FAILED, "进程启动失败，请检查系统资源"},
            {ErrorCode::FFMPEG_COMMAND_EMPTY, "FFmpeg 命令为空，请检查配置"},
            {ErrorCode::FFMPEG_COMMAND_BUILDER_MISSING, "FFmpeg 命令构建器缺失，请检查 Gateway 配置"},
            {ErrorCode::FFMPEG_START_FAILED, "FFmpeg 启动失败，请检查 FFmpeg 路径和权限"},
            
            // 流验证相关
            {ErrorCode::STREAM_VALIDATION_FAILED, "流验证失败，FFmpeg 进程运行但流未在 ZLM 中注册"},
            {ErrorCode::STREAM_NOT_READY, "流未就绪，请稍后重试"},
            {ErrorCode::STREAM_TIMEOUT, "流启动超时，请检查源流和网络连接"},
            
            // ZLM API 相关
            {ErrorCode::ZLM_API_FAILED, "ZLM API 调用失败，请检查 ZLM 服务状态"},
            {ErrorCode::ZLM_NOT_AVAILABLE, "ZLM 服务不可用，请检查 ZLM 服务是否运行"},
            {ErrorCode::ZLM_STREAM_NOT_FOUND, "ZLM 中流不存在，请检查流是否已创建"},
            
            // 设备相关
            {ErrorCode::DEVICE_NOT_FOUND, "设备不存在，请检查设备 ID 或刷新设备列表"},
            {ErrorCode::DEVICE_BUSY, "设备忙碌，该设备可能正在被其他流使用"},
            {ErrorCode::DEVICE_OFFLINE, "设备离线，请检查设备连接状态"},
            
            // 配置相关
            {ErrorCode::INVALID_CONFIG, "配置无效，请检查配置文件"},
            {ErrorCode::UNSUPPORTED_SOURCE_TYPE, "不支持的源类型"},
            
            // 其他
            {ErrorCode::TIMEOUT, "操作超时，请稍后重试"},
            {ErrorCode::UNKNOWN_ERROR, "未知错误，请查看服务日志获取详细信息"},
        };
        
        auto it = error_messages.find(error_code);
        if (it != error_messages.end()) {
            return it->second;
        }
        
        // 如果找不到对应的错误消息，返回错误码本身
        return error_code.empty() ? "未知错误" : error_code;
    }
};

} // namespace utils
} // namespace gateway

#endif // GATEWAY_UTILS_ERROR_CODES_HPP

