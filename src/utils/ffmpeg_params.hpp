#ifndef UTILS_FFMPEG_PARAMS_HPP
#define UTILS_FFMPEG_PARAMS_HPP

#include <string>
#include <sstream>

namespace utils {

/**
 * @brief FFmpeg参数构建工具
 * 
 * 提供统一的FFmpeg参数构建方法，避免代码重复
 */
class FFmpegParams {
public:
    /**
     * @brief 添加视频码率参数
     * @param oss 输出流
     * @param bitrate_kbps 码率（kbps），如果为0则不添加
     */
    static void AddVideoBitrateParams(std::ostringstream& oss, int bitrate_kbps);
    
    /**
     * @brief 添加传输速度限制参数（用于copy模式）
     * @param oss 输出流
     * @param bitrate_kbps 码率（kbps），如果为0则不添加
     */
    static void AddRateLimitParams(std::ostringstream& oss, int bitrate_kbps);
    
    /**
     * @brief 添加WebRTC转码参数
     * @param oss 输出流
     * @param bitrate_kbps 码率（kbps），如果为0则使用默认值2500
     * @param include_fpsmax 是否包含-fpsmax参数（已废弃：由于与-r参数冲突，不再使用）
     * @param source_width 源流宽度（0表示未检测到，将使用默认缩放）
     * @param source_height 源流高度（0表示未检测到，将使用默认缩放）
     */
    static void AddWebRTCEncodingParams(std::ostringstream& oss, 
                                       int bitrate_kbps, 
                                       bool include_fpsmax = true,
                                       int source_width = 0,
                                       int source_height = 0);
    
    /**
     * @brief 添加流媒体编码参数（用于推流到ZLM）
     * 
     * 注意：此函数只添加编码参数（codec、GOP、码率等），不涉及输出格式。
     * 所有Gateway都推流到ZLM的RTMP端口（使用 -f flv），
     * ZLM会自动将流转换为多种输出协议（HTTP-FLV、HLS、WebRTC等）。
     * 
     * @param oss 输出流
     * @param bitrate_kbps 码率（kbps），如果为0则使用默认值3500
     * @param source_width 源流宽度（0表示未检测到，将使用默认缩放）
     * @param source_height 源流高度（0表示未检测到，将使用默认缩放）
     * @param output_protocol 输出协议（"http-flv" 或 "hls"），用于调整GOP大小
     *                        - HLS: GOP=12（降低首屏延迟）
     *                        - HTTP-FLV: GOP=18（平衡延迟和质量）
     */
    static void AddStreamingEncodingParams(std::ostringstream& oss, 
                                            int bitrate_kbps,
                                            int source_width = 0,
                                            int source_height = 0,
                                            const std::string& output_protocol = "");
};

} // namespace utils

#endif // UTILS_FFMPEG_PARAMS_HPP

