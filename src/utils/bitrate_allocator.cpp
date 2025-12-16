#include "utils/bitrate_allocator.hpp"
#include "utils/logger.hpp"
#include "streaming/stream_manager.hpp"
#include <algorithm>
#include <cmath>
#include <sstream>
#include <chrono>

namespace utils {

BitrateAllocator::BitrateAllocator(const BitrateAllocatorConfig& config,
                                   std::shared_ptr<streaming::ZLMClient> zlm_client)
    : config_(config), zlm_client_(zlm_client) {
    last_update_time_ = 0;
}

BitrateAllocator::~BitrateAllocator() = default;

int BitrateAllocator::GetRecommendedBitrate(const std::string& app,
                                            const std::string& stream,
                                            const std::string& /* protocol */,
                                            const std::string& output_protocol,
                                            const std::string& resolution,
                                            int fps,
                                            int priority,
                                            StreamAllocationStatus status,
                                            int64_t create_time) {
    std::lock_guard<std::mutex> lock(mutex_);

    // 检查是否需要更新
    auto now = std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    
    // CRITICAL FIX: Use UpdateAllocationsUnlocked() to avoid deadlock
    // Since we already hold mutex_, calling UpdateAllocations() would deadlock
    if (config_.enable_adaptive && 
        (now - last_update_time_) >= config_.update_interval_sec) {
        UpdateAllocationsUnlocked();  // Call unlocked version
    }

    // 查找流的码率信息
    std::string stream_key = app + "/" + stream;
    auto it = stream_infos_.find(stream_key);
    if (it != stream_infos_.end() && it->second) {
        return it->second->current_bitrate_kbps;
    }

    // 如果没有找到，计算推荐码率（智能版本）
    int base_bitrate = CalculateBaseBitrate(resolution, fps);
    base_bitrate = AdjustBitrateByPriority(base_bitrate, priority);
    base_bitrate = AdjustBitrateByProtocol(base_bitrate, output_protocol);
    base_bitrate = AdjustBitrateByStatus(base_bitrate, status);
    
    // 如果提供了创建时间，计算运行时长并调整
    if (create_time > 0) {
        int64_t running_duration = now - create_time;
        double stability = 1.0;
        if (running_duration > config_.min_stable_duration_sec) {
            stability = 1.0;
        } else {
            stability = 0.5 + (running_duration * 0.5 / config_.min_stable_duration_sec);
        }
        base_bitrate = AdjustBitrateByStability(base_bitrate, stability, running_duration);
    }
    
    // 限制在最小和最大码率之间
    base_bitrate = std::max(config_.min_bitrate_kbps, 
                           std::min(config_.max_bitrate_kbps, base_bitrate));
    
    return base_bitrate;
}

bool BitrateAllocator::UpdateAllocations(std::shared_ptr<streaming::StreamManager> stream_manager) {
    std::lock_guard<std::mutex> lock(mutex_);
    return UpdateAllocationsUnlocked(stream_manager);
}

// Internal unlocked version - must be called with mutex_ already held
bool BitrateAllocator::UpdateAllocationsUnlocked(std::shared_ptr<streaming::StreamManager> stream_manager) {
    // NO LOCKING - caller must hold mutex_
    try {
        // 获取所有活跃流
        auto zlm_streams = FetchActiveStreams();
        
        // 转换为StreamBitrateInfo（智能版本）
        std::vector<StreamBitrateInfo> stream_infos;
        auto now = std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();
        
        for (const auto& zlm_stream : zlm_streams) {
            StreamBitrateInfo info;
            info.app = zlm_stream.app;
            info.stream = zlm_stream.stream;
            info.reader_count = zlm_stream.reader_count;
            info.current_bytes_speed = zlm_stream.bytes_speed;
            info.current_bitrate_kbps = zlm_stream.bytes_speed * 8 / 1000; // 转换为kbps
            info.is_active = streaming::ZLMClient::IsStreamActive(zlm_stream);
            
            // 从StreamManager获取流元数据（如果提供）
            std::string stream_key = info.app + "/" + info.stream;
            if (stream_manager) {
                auto metadata = stream_manager->GetStreamMetadata(info.app, info.stream);
                if (metadata.status != streaming::StreamStatus::Stopped) {
                    info.protocol = metadata.protocol;
                    info.output_protocol = metadata.output_protocol;
                    info.create_time = metadata.create_time;
                    info.running_duration_sec = now - metadata.create_time;
                    
                    // 根据StreamStatus转换为StreamAllocationStatus
                    if (metadata.status == streaming::StreamStatus::Starting) {
                        info.status = StreamAllocationStatus::Starting;
                    } else if (metadata.status == streaming::StreamStatus::Running) {
                        info.status = StreamAllocationStatus::Running;
                    } else if (metadata.status == streaming::StreamStatus::Stopping) {
                        info.status = StreamAllocationStatus::Stopping;
                    } else if (metadata.status == streaming::StreamStatus::Error) {
                        info.status = StreamAllocationStatus::Error;
                    } else {
                        info.status = StreamAllocationStatus::Running;
                    }
                }
            }
            
            // 从现有信息中获取其他字段
            auto it = stream_infos_.find(stream_key);
            if (it != stream_infos_.end() && it->second) {
                // 保留历史信息
                info.priority = it->second->priority;
                info.resolution = it->second->resolution;
                info.fps = it->second->fps;
                info.stability_score = it->second->stability_score;
                info.consecutive_errors = it->second->consecutive_errors;
                info.network_quality = it->second->network_quality;
                info.requires_low_latency = it->second->requires_low_latency;
                info.min_quality_bitrate = it->second->min_quality_bitrate;
                info.preferred_bitrate = it->second->preferred_bitrate;
                
                // 如果没有从StreamManager获取状态，使用历史状态
                if (info.status == StreamAllocationStatus::Running && 
                    it->second->status != StreamAllocationStatus::Running) {
                    info.status = it->second->status;
                }
            } else {
                // 默认值
                if (info.protocol.empty()) info.protocol = "";
                if (info.output_protocol.empty()) info.output_protocol = "";
                info.priority = 5;
                info.resolution = "1280x720";
                info.fps = 30;
                info.status = StreamAllocationStatus::Running;
                info.stability_score = 0.5; // 新流，稳定性较低
                info.network_quality = 1.0; // 默认网络质量好
                info.requires_low_latency = (info.output_protocol == "webrtc");
            }
            
            // 计算稳定性和网络质量
            info.stability_score = CalculateStabilityScore(info);
            info.network_quality = CalculateNetworkQuality(info);
            
            stream_infos.push_back(info);
        }

        // 根据流数量自适应调整总带宽
        int adjusted_bandwidth = AdjustTotalBandwidthByStreamCount(stream_infos.size());
        
        // 分配码率
        auto allocations = AllocateBitrates(stream_infos, adjusted_bandwidth);

        // 更新流信息
        for (auto& info : stream_infos) {
            std::string stream_key = info.app + "/" + info.stream;
            auto alloc_it = allocations.find(stream_key);
            if (alloc_it != allocations.end()) {
                info.current_bitrate_kbps = alloc_it->second;
            }
            
            // 更新或创建流信息
            if (stream_infos_.find(stream_key) == stream_infos_.end()) {
                stream_infos_[stream_key] = std::make_shared<StreamBitrateInfo>(info);
            } else {
                *stream_infos_[stream_key] = info;
            }
        }

        // 清理不活跃的流
        std::vector<std::string> to_remove;
        for (const auto& [key, info] : stream_infos_) {
            bool found = false;
            for (const auto& active_info : stream_infos) {
                if (active_info.app == info->app && active_info.stream == info->stream) {
                    found = true;
                    break;
                }
            }
            if (!found) {
                to_remove.push_back(key);
            }
        }
        for (const auto& key : to_remove) {
            stream_infos_.erase(key);
        }

        last_update_time_ = std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();

        LOG_DEBUG("[BitrateAllocator] Updated allocations for {} streams", stream_infos.size());
        return true;
    } catch (const std::exception& e) {
        LOG_ERROR("[BitrateAllocator] Failed to update allocations: {}", e.what());
        return false;
    }
}

std::shared_ptr<StreamBitrateInfo> BitrateAllocator::GetStreamInfo(
    const std::string& app,
    const std::string& stream) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    std::string stream_key = app + "/" + stream;
    auto it = stream_infos_.find(stream_key);
    if (it != stream_infos_.end()) {
        return it->second;
    }
    return nullptr;
}

std::vector<StreamBitrateInfo> BitrateAllocator::GetAllAllocations() {
    std::lock_guard<std::mutex> lock(mutex_);
    
    std::vector<StreamBitrateInfo> result;
    for (const auto& [key, info] : stream_infos_) {
        if (info) {
            result.push_back(*info);
        }
    }
    return result;
}

void BitrateAllocator::SetConfig(const BitrateAllocatorConfig& config) {
    std::lock_guard<std::mutex> lock(mutex_);
    config_ = config;
}

BitrateAllocatorConfig BitrateAllocator::GetConfig() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return config_;
}

std::vector<streaming::StreamInfo> BitrateAllocator::FetchActiveStreams() {
    if (!zlm_client_) {
        return {};
    }
    
    try {
        return zlm_client_->GetStreamList();
    } catch (const std::exception& e) {
        LOG_ERROR("[BitrateAllocator] Failed to fetch streams: {}", e.what());
        return {};
    }
}

int BitrateAllocator::CalculateBaseBitrate(const std::string& resolution, int fps) {
    if (resolution.empty()) {
        return config_.default_bitrate_kbps;
    }

    // 解析分辨率
    size_t x_pos = resolution.find('x');
    if (x_pos == std::string::npos) {
        return config_.default_bitrate_kbps;
    }

    int width = 0, height = 0;
    try {
        width = std::stoi(resolution.substr(0, x_pos));
        height = std::stoi(resolution.substr(x_pos + 1));
    } catch (...) {
        return config_.default_bitrate_kbps;
    }

    // 计算像素数
    int pixels = width * height;
    
    // 根据分辨率和帧率计算基础码率
    // 公式：码率 = 像素数 * 帧率 * 系数
    // 系数根据分辨率调整（更“豪华”的默认画质）：
    // - 1080p: 0.14 bpp (bits per pixel)
    // - 720p: 0.10 bpp
    // - 480p: 0.06 bpp
    double bpp = 0.1; // 默认值（720p附近）
    if (height >= 1080) {
        bpp = 0.14;
    } else if (height >= 720) {
        bpp = 0.10;
    } else if (height >= 480) {
        bpp = 0.06;
    } else {
        bpp = 0.04;
    }

    int bitrate = static_cast<int>(pixels * fps * bpp / 1000.0); // 转换为kbps
    
    // 限制在合理范围内
    return std::max(config_.min_bitrate_kbps, 
                   std::min(config_.max_bitrate_kbps, bitrate));
}

int BitrateAllocator::AdjustBitrateByPriority(int base_bitrate, int priority) {
    // 优先级范围：1-10，10最高
    // 优先级越高，码率越高（最高+50%，最低-30%）
    priority = std::max(1, std::min(10, priority));
    double factor = 0.7 + (priority - 1) * 0.08; // 0.7 ~ 1.42
    return static_cast<int>(base_bitrate * factor);
}

int BitrateAllocator::AdjustBitrateByViewers(int base_bitrate, int reader_count) {
    // 观看者越多，码率可以适当提高（最多+20%）
    if (reader_count <= 0) {
        return base_bitrate;
    }
    
    double factor = 1.0 + std::min(reader_count * 0.05, 0.2); // 每个观看者+5%，最多+20%
    return static_cast<int>(base_bitrate * factor);
}

std::map<std::string, int> BitrateAllocator::AllocateBitrates(
    const std::vector<StreamBitrateInfo>& streams,
    int total_bandwidth_kbps) {
    std::map<std::string, int> allocations;

    if (streams.empty() || total_bandwidth_kbps <= 0) {
        return allocations;
    }

    // 先统计「活跃流」数量
    std::vector<std::string> active_keys;
    active_keys.reserve(streams.size());
    for (const auto& stream : streams) {
        if (!stream.is_active) {
            continue;
        }
        active_keys.emplace_back(stream.app + "/" + stream.stream);
    }

    if (active_keys.empty()) {
        return allocations;
    }

    // 按流数量平均分配带宽 —— 所有流权重相同
    int per_stream_kbps = total_bandwidth_kbps / static_cast<int>(active_keys.size());
    if (per_stream_kbps <= 0) {
        per_stream_kbps = config_.min_bitrate_kbps;
    }

    for (const auto& key : active_keys) {
        int allocated = std::max(config_.min_bitrate_kbps,
                                 std::min(config_.max_bitrate_kbps, per_stream_kbps));
        allocations[key] = allocated;
    }

    LOG_DEBUG("[BitrateAllocator] (equal-share) Allocated {} kbps per stream for {} streams, total bandwidth: {} kbps",
              per_stream_kbps, active_keys.size(), total_bandwidth_kbps);

    return allocations;
}

// 增强方法的实现
int BitrateAllocator::AdjustBitrateByStatus(int base_bitrate, StreamAllocationStatus status) {
    switch (status) {
        case StreamAllocationStatus::Starting:
            return static_cast<int>(base_bitrate * config_.starting_bitrate_factor);
        case StreamAllocationStatus::Running:
            return base_bitrate;
        case StreamAllocationStatus::Stopping:
            return static_cast<int>(base_bitrate * config_.stopping_bitrate_factor);
        case StreamAllocationStatus::Error:
            return config_.min_bitrate_kbps;
        default:
            return base_bitrate;
    }
}

int BitrateAllocator::AdjustBitrateByProtocol(int base_bitrate, const std::string& output_protocol) {
    if (!config_.enable_protocol_optimization) {
        return base_bitrate;
    }
    if (output_protocol == "webrtc") {
        return static_cast<int>(base_bitrate * 0.8);
    } else if (output_protocol == "hls") {
        return static_cast<int>(base_bitrate * 1.1);
    }
    return base_bitrate;
}

int BitrateAllocator::AdjustBitrateByStability(int base_bitrate, double stability_score, int64_t running_duration_sec) {
    double factor = 0.7 + (stability_score * 0.3);
    if (running_duration_sec > config_.min_stable_duration_sec) {
        factor = std::min(1.0, factor + 0.1);
    }
    return static_cast<int>(base_bitrate * factor);
}

int BitrateAllocator::AdjustBitrateByNetworkQuality(int base_bitrate, double network_quality) {
    double factor = 0.8 + (network_quality * 0.2);
    return static_cast<int>(base_bitrate * factor);
}

double BitrateAllocator::CalculateStabilityScore(const StreamBitrateInfo& stream_info) {
    double score = 1.0;
    if (stream_info.consecutive_errors > 0) {
        score -= stream_info.consecutive_errors * 0.1;
        score = std::max(0.0, score);
    }
    if (stream_info.running_duration_sec > config_.min_stable_duration_sec) {
        score = std::min(1.0, score + 0.2);
    } else if (stream_info.running_duration_sec > 0) {
        double progress = static_cast<double>(stream_info.running_duration_sec) / config_.min_stable_duration_sec;
        score = std::min(1.0, score + progress * 0.2);
    }
    if (stream_info.status == StreamAllocationStatus::Error) {
        score *= 0.5;
    } else if (stream_info.status == StreamAllocationStatus::Starting) {
        score *= 0.7;
    }
    return std::max(0.0, std::min(1.0, score));
}

double BitrateAllocator::CalculateNetworkQuality(const StreamBitrateInfo& stream_info) {
    if (stream_info.current_bitrate_kbps <= 0) {
        return 1.0;
    }
    double efficiency = static_cast<double>(stream_info.current_bytes_speed * 8 / 1000) / 
                       std::max(1, stream_info.current_bitrate_kbps);
    if (efficiency >= 0.8 && efficiency <= 1.2) {
        return 1.0;
    } else if (efficiency < 0.8) {
        return std::max(0.3, efficiency);
    } else {
        return std::min(1.0, 1.0 + (efficiency - 1.2) * 0.5);
    }
}

int BitrateAllocator::AdjustTotalBandwidthByStreamCount(size_t stream_count) {
    int base_bandwidth = (config_.total_bandwidth_mbps - config_.reserved_bandwidth_mbps) * 1000;
    if (stream_count == 0) return base_bandwidth;
    if (stream_count <= 3) return static_cast<int>(base_bandwidth * 0.9);
    else if (stream_count <= 10) return static_cast<int>(base_bandwidth * 0.95);
    else return static_cast<int>(base_bandwidth * 0.98);
}

} // namespace utils

