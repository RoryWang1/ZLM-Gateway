#ifndef GATEWAY_UTILS_PROTOCOL_CONSTANTS_HPP
#define GATEWAY_UTILS_PROTOCOL_CONSTANTS_HPP

namespace gateway {
namespace Protocol {

// 流媒体协议
constexpr const char* RTSP = "rtsp";
constexpr const char* RTMP = "rtmp";
constexpr const char* HTTP_FLV = "http-flv";
constexpr const char* HLS = "hls";
constexpr const char* DASH = "dash";
constexpr const char* QUIC = "quic";

// 设备协议
constexpr const char* ONVIF = "onvif";
constexpr const char* ISAPI = "isapi";
constexpr const char* DAHUA = "dahua";
constexpr const char* PSIA = "psia";
constexpr const char* GB28181 = "gb28181";

// 其他协议
constexpr const char* LOCAL_CAMERA = "local-camera";
constexpr const char* WEBRTC = "webrtc";
constexpr const char* FLV = "flv";  // ZLM schema 别名

} // namespace Protocol

// Gateway Type 命名（用于内部标识）
namespace GatewayType {

constexpr const char* RTSP = "rtsp_gateway";
constexpr const char* RTMP = "rtmp_gateway";
constexpr const char* HTTP_FLV = "httpflv_gateway";
constexpr const char* HLS = "hls_gateway";
constexpr const char* DASH = "dash_gateway";
constexpr const char* QUIC = "quic_gateway";
constexpr const char* ONVIF = "onvif_gateway";
constexpr const char* ISAPI = "isapi_gateway";
constexpr const char* DAHUA = "dahua_gateway";
constexpr const char* PSIA = "psia_gateway";
constexpr const char* GB28181 = "gb28181_gateway";
constexpr const char* LOCAL_CAMERA = "local_camera_gateway";

} // namespace GatewayType

} // namespace gateway

#endif // GATEWAY_UTILS_PROTOCOL_CONSTANTS_HPP
