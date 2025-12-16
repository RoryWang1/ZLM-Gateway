#include "gateway/local_camera/device_manager.hpp"
#include "gateway/local_camera/local_camera_gateway.hpp"  // 需要 LocalCameraDevice 的完整定义
#include "utils/camera_detector.hpp"
#include "utils/logger.hpp"
#include "api/websocket_server.hpp"
#include "config/config_loader.hpp"
#include <algorithm>
#include <nlohmann/json.hpp>

namespace gateway {
namespace local_camera {

DeviceManager::DeviceManager(std::shared_ptr<config::Config> config,
                            std::unique_ptr<::utils::CameraDetector>& camera_detector,
                            std::shared_ptr<api::WebSocketServer> websocket_server)
    : config_(config), camera_detector_(camera_detector), websocket_server_(websocket_server),
      last_discovery_time_(std::chrono::steady_clock::now() - std::chrono::hours(1)),
      capability_cache_ttl_seconds_(300) {
}

std::vector<gateway::LocalCameraDevice> DeviceManager::DiscoverCameras() {
    auto devices = camera_detector_->DiscoverCameras();
    
    // 发现音频设备（用于自动匹配）
    std::vector<::utils::AudioDevice> audio_devices = camera_detector_->DiscoverAudioDevices();
    
    std::vector<gateway::LocalCameraDevice> result;
    result.reserve(devices.size());
    
    std::lock_guard<std::mutex> lock(cameras_mutex_);
    
    for (const auto& device : devices) {
        gateway::LocalCameraDevice local_device;
        local_device.device_id = device.device_id;
        local_device.name = device.name;
        local_device.index = device.index;
        local_device.platform = device.platform;
        local_device.device_path = device.device_path;
        
        // 自动匹配音频设备
        if (!audio_devices.empty()) {
            int matched_audio_index = camera_detector_->MatchAudioDevice(device.name, audio_devices);
            local_device.audio_device_index = matched_audio_index;
            if (matched_audio_index >= 0) {
                LOG_DEBUG("[DeviceManager] 摄像头 '{}' 匹配到音频设备索引: {}", 
                         device.name, matched_audio_index);
            }
        }
        
        // 保存到设备列表
        cameras_[device.device_id] = local_device;
        result.push_back(local_device);
    }
    
    last_discovery_time_ = std::chrono::steady_clock::now();
    LOG_INFO("[DeviceManager] 发现 {} 个摄像头设备", result.size());
    return result;
}

std::vector<gateway::LocalCameraDevice> DeviceManager::ListCameras(bool refresh) {
    // 优化：先检查缓存状态（不持有锁），减少锁持有时间
    bool cache_expired = false;
    bool need_discover = refresh;
    
    {
        std::lock_guard<std::mutex> lock(cameras_mutex_);
        cache_expired = IsCacheExpired();
        
        // 如果不需要刷新且缓存未过期，直接返回缓存的设备列表
        if (!refresh && !cache_expired) {
            auto now = std::chrono::steady_clock::now();
            auto cache_age = std::chrono::duration_cast<std::chrono::seconds>(now - last_discovery_time_).count();
            std::vector<gateway::LocalCameraDevice> result;
            result.reserve(cameras_.size());
            for (const auto& pair : cameras_) {
                result.push_back(pair.second);
            }
            LOG_DEBUG("[DeviceManager] 返回缓存的设备列表（缓存年龄: {}秒）", cache_age);
            return result;
        }
        
        need_discover = refresh || cache_expired;
    }  // 释放锁
    
    // 缓存过期或需要刷新，重新扫描设备以检测插拔变化
    // 注意：DiscoverCameras() 可能很慢（执行系统命令），所以在锁外执行
    if (need_discover) {
        LOG_DEBUG("[DeviceManager] 设备缓存{}，重新扫描设备...", 
                 cache_expired ? "已过期" : "需要刷新");
        auto devices = camera_detector_->DiscoverCameras();
        
        // 重新加锁更新设备列表
        {
            std::lock_guard<std::mutex> lock(cameras_mutex_);
            auto now = std::chrono::steady_clock::now();
            last_discovery_time_ = now;
            
            // 更新设备列表（检测新增和移除的设备）
            UpdateDeviceList(devices);
        }  // 释放锁
    }
    
    // 最后加锁构建返回结果
    std::lock_guard<std::mutex> lock(cameras_mutex_);
    std::vector<gateway::LocalCameraDevice> result;
    result.reserve(cameras_.size());
    for (const auto& pair : cameras_) {
        result.push_back(pair.second);
    }
    
    return result;
}

gateway::LocalCameraDevice DeviceManager::GetCamera(const std::string& device_id) const {
    std::lock_guard<std::mutex> lock(cameras_mutex_);
    
    // 检查缓存是否过期
    if (IsCacheExpired()) {
        auto now = std::chrono::steady_clock::now();
        auto cache_age = std::chrono::duration_cast<std::chrono::seconds>(now - last_discovery_time_).count();
        LOG_DEBUG("[DeviceManager] 设备缓存已过期（{}秒），GetCamera 返回空对象，建议调用 ListCameras(true) 刷新", cache_age);
        gateway::LocalCameraDevice empty;
        empty.device_id = "";
        return empty;
    }
    
    auto it = cameras_.find(device_id);
    if (it == cameras_.end()) {
        gateway::LocalCameraDevice empty;
        empty.device_id = "";
        return empty;
    }
    
    return it->second;
}

int DeviceManager::GetCameraIndexFromDeviceID(const std::string& device_id) const {
    std::lock_guard<std::mutex> lock(cameras_mutex_);
    
    auto it = cameras_.find(device_id);
    if (it == cameras_.end()) {
        return -1;
    }
    
    return it->second.index;
}

std::pair<std::vector<std::string>, std::vector<int>> DeviceManager::QueryDeviceCapabilities(const std::string& device_id) const {
    gateway::LocalCameraDevice camera = GetCamera(device_id);
    if (camera.device_id.empty()) {
        LOG_WARN("[DeviceManager] 设备不存在，无法查询能力: {}", device_id);
        return {{}, {}};
    }
    
    // 如果设备信息中已有能力信息，直接返回
    if (!camera.supported_resolutions.empty() || !camera.supported_fps.empty()) {
        return {camera.supported_resolutions, camera.supported_fps};
    }
    
    // 检查缓存
    {
        std::lock_guard<std::mutex> lock(capability_cache_mutex_);
        auto it = capability_cache_.find(device_id);
        if (it != capability_cache_.end()) {
            // 检查缓存是否过期
            auto now = std::chrono::steady_clock::now();
            auto cache_age = std::chrono::duration_cast<std::chrono::seconds>(now - it->second.cache_time).count();
            if (cache_age < capability_cache_ttl_seconds_) {
                LOG_DEBUG("[DeviceManager] 从缓存返回设备能力: {}", device_id);
                return {it->second.resolutions, it->second.fps};
            } else {
                // 缓存过期，删除
                capability_cache_.erase(it);
            }
        }
    }
    
    // 通过 CameraDetector 查询
    std::pair<std::vector<std::string>, std::vector<int>> capabilities = {{}, {}};
    if (camera_detector_) {
        capabilities = camera_detector_->QueryCameraCapabilities(camera.index);
        
        // 更新缓存
        if (!capabilities.first.empty() || !capabilities.second.empty()) {
            std::lock_guard<std::mutex> lock(capability_cache_mutex_);
            CapabilityCacheEntry entry;
            entry.resolutions = capabilities.first;
            entry.fps = capabilities.second;
            entry.cache_time = std::chrono::steady_clock::now();
            capability_cache_[device_id] = entry;
        }
    }
    
    return capabilities;
}

bool DeviceManager::IsCacheExpired() const {
    auto now = std::chrono::steady_clock::now();
    auto cache_age = std::chrono::duration_cast<std::chrono::seconds>(now - last_discovery_time_).count();
    return cache_age >= config_->local_camera.device_cache_ttl_seconds;
}

void DeviceManager::UpdateDeviceList(const std::vector<::utils::CameraDevice>& devices) {
    // 保存旧的设备ID集合，用于检测变化
    std::set<std::string> old_device_ids;
    for (const auto& pair : cameras_) {
        old_device_ids.insert(pair.first);
    }
    
    // 发现音频设备（用于自动匹配）
    std::vector<::utils::AudioDevice> audio_devices = camera_detector_->DiscoverAudioDevices();
    
    // 更新设备列表：添加新设备，移除已拔掉的设备
    std::set<std::string> current_device_ids;
    for (const auto& device : devices) {
        current_device_ids.insert(device.device_id);
        
        // 检查是否是新设备
        bool is_new_device = (old_device_ids.find(device.device_id) == old_device_ids.end());
        
        // 更新或添加设备信息（即使设备已存在，也要更新索引，因为索引可能会变化）
        gateway::LocalCameraDevice local_device;
        local_device.device_id = device.device_id;
        local_device.name = device.name;
        local_device.index = device.index;
        local_device.platform = device.platform;
        local_device.device_path = device.device_path;
        
        // 自动匹配音频设备
        if (!audio_devices.empty()) {
            int matched_audio_index = camera_detector_->MatchAudioDevice(device.name, audio_devices);
            local_device.audio_device_index = matched_audio_index;
        }
        
        // 如果设备已存在但索引变化，记录日志
        auto existing_it = cameras_.find(device.device_id);
        if (existing_it != cameras_.end() && existing_it->second.index != device.index) {
            LOG_INFO("[DeviceManager] 设备索引变化: {} (device_id: {}) 从索引 {} 变为 {}", 
                    device.name, device.device_id, existing_it->second.index, device.index);
        }
        
        cameras_[device.device_id] = local_device;
        
        // 如果是新设备，通过 WebSocket 推送
        if (is_new_device) {
            BroadcastDeviceUpdate(local_device, "added");
        }
    }
    
    // 移除已拔掉的设备
    auto it = cameras_.begin();
    while (it != cameras_.end()) {
        if (current_device_ids.find(it->first) == current_device_ids.end()) {
            LOG_INFO("[DeviceManager] 设备已移除: {} ({})", it->second.name, it->first);
            BroadcastDeviceUpdate(it->second, "removed");
            it = cameras_.erase(it);
        } else {
            ++it;
        }
    }
}

void DeviceManager::BroadcastDeviceUpdate(const gateway::LocalCameraDevice& device, const std::string& action) const {
    if (!websocket_server_) {
        return;
    }
    
    nlohmann::json device_data = {
        {"protocol", "local-camera"},
        {"action", action},
        {"device", {
            {"device_id", device.device_id},
            {"name", device.name}
        }}
    };
    
    if (action == "added") {
        device_data["device"]["index"] = device.index;
        device_data["device"]["platform"] = device.platform;
        device_data["device"]["device_path"] = device.device_path;
    }
    
    websocket_server_->BroadcastDeviceUpdate(device_data);
    
    if (action == "added") {
        LOG_INFO("[DeviceManager] 通过 WebSocket 推送新设备: {} ({})", device.name, device.device_id);
    }
}

} // namespace local_camera
} // namespace gateway

