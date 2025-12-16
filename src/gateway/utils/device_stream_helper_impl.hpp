#ifndef GATEWAY_UTILS_DEVICE_STREAM_HELPER_IMPL_HPP
#define GATEWAY_UTILS_DEVICE_STREAM_HELPER_IMPL_HPP

#include "gateway/utils/device_stream_helper.hpp"
#include "gateway/utils/error_codes.hpp"
#include "streaming/zlmediakit/zlm_client.hpp"
#include "streaming/stream_manager.hpp"
#include "utils/logger.hpp"
#include <mutex>

namespace gateway {
namespace utils {

template<typename StreamInfoType>
bool DeviceStreamHelper<StreamInfoType>::StartDeviceStream(
    const std::string& stream_id,
    const std::string& device_id,
    const std::string& channel_id,
    const std::string& target_app,
    const std::string& target_stream,
    std::map<std::string, StreamInfoType>& streams,
    std::mutex& mutex,
    std::shared_ptr<streaming::ZLMClient> zlm_client,
    std::shared_ptr<streaming::StreamManager> stream_manager,
    std::function<std::vector<std::string>(const std::string&, const std::string&)> get_rtsp_urls_callback,
    const std::string& gateway_name,
    const std::string& protocol) {
    
    // 通过 StreamManager 统一记录"创建请求已发起"，状态设为 Starting
    if (stream_manager) {
        streaming::StreamMetadata metadata;
        metadata.app = target_app;
        metadata.stream = target_stream;
        metadata.protocol = protocol;
        metadata.source_url = protocol + "://" + device_id + (channel_id.empty() ? "" : "/" + channel_id);
        metadata.gateway_type = protocol + "_gateway";
        metadata.status = streaming::StreamStatus::Starting;
        stream_manager->OnStreamCreateRequested(metadata);
    }
    
    std::lock_guard<std::mutex> stream_lock(mutex);
    
    auto it = streams.find(stream_id);
    if (it != streams.end() && it->second.status == GatewayStatus::Running) {
        ::utils::Logger::Get()->warn("流已运行: {}/{}", target_app, target_stream);
        if (stream_manager) {
            stream_manager->OnStreamCreateResult(target_app, target_stream, true);
        }
        return true;
    }
    
    std::vector<std::string> rtsp_urls = get_rtsp_urls_callback(device_id, channel_id);
    if (rtsp_urls.empty()) {
        ::utils::Logger::Get()->error("无法获取设备 RTSP 地址: {}", device_id);
        if (stream_manager) {
            using namespace gateway::utils;
            stream_manager->OnStreamCreateResult(target_app, target_stream, false, 
                                                 ErrorCode::DEVICE_NOT_FOUND, 
                                                 ErrorMessageMapper::GetMessage(ErrorCode::DEVICE_NOT_FOUND));
        }
        return false;
    }
    
    std::string rtsp_url = rtsp_urls[0];
    
    if (!zlm_client->AddRTSPStream(target_app, target_stream, rtsp_url)) {
        ::utils::Logger::Get()->error("添加 RTSP 流到 ZLMediaKit 失败: {}/{}", target_app, target_stream);
        if (stream_manager) {
            using namespace gateway::utils;
            stream_manager->OnStreamCreateResult(target_app, target_stream, false, 
                                                 ErrorCode::ZLM_API_FAILED, 
                                                 ErrorMessageMapper::GetMessage(ErrorCode::ZLM_API_FAILED));
        }
        return false;
    }
    
    StreamInfoType info;
    info.device_id = device_id;
    info.channel_id = channel_id.empty() ? "1" : channel_id;
    info.rtsp_url = rtsp_url;
    info.target_app = target_app;
    info.target_stream = target_stream;
    info.status = GatewayStatus::Running;  // 保留内部状态，但状态机由 StreamManager 统一管理
    
    streams[stream_id] = info;
    
    // 通过 StreamManager 统一记录"创建结果"
    if (stream_manager) {
        stream_manager->OnStreamCreateResult(target_app, target_stream, true);
    }
    
    ::utils::Logger::Get()->info("{} Gateway 启动成功: 设备 {} -> {}/{}", gateway_name, device_id, target_app, target_stream);
    return true;
}

template<typename StreamInfoType>
bool DeviceStreamHelper<StreamInfoType>::StopDeviceStream(
    const std::string& stream_id,
    const std::string& target_app,
    const std::string& target_stream,
    std::map<std::string, StreamInfoType>& streams,
    std::mutex& mutex,
    std::shared_ptr<streaming::ZLMClient> zlm_client,
    const std::string& gateway_name) {
    
    std::lock_guard<std::mutex> stream_lock(mutex);
    
    auto it = streams.find(stream_id);
    if (it == streams.end()) {
        ::utils::Logger::Get()->warn("流不存在: {}/{}", target_app, target_stream);
        return false;
    }
    
    bool success = zlm_client->DeleteStream(target_app, target_stream);
    streams.erase(it);
    
    if (success) {
        ::utils::Logger::Get()->info("{} Gateway 停止成功: {}/{}", gateway_name, target_app, target_stream);
    } else {
        ::utils::Logger::Get()->warn("{} Gateway 停止失败（流可能已不存在）: {}/{}", gateway_name, target_app, target_stream);
    }
    
    return success;
}

} // namespace utils
} // namespace gateway

#endif // GATEWAY_UTILS_DEVICE_STREAM_HELPER_IMPL_HPP

