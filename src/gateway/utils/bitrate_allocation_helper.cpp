#include "gateway/utils/bitrate_allocation_helper.hpp"
#include "utils/bitrate_allocator.hpp"
#include "utils/logger.hpp"
#include <chrono>

namespace gateway {
namespace utils {

BitrateAllocationHelper::BitrateAllocationHelper(std::shared_ptr<::utils::BitrateAllocator> bitrate_allocator)
    : bitrate_allocator_(bitrate_allocator) {
}

int BitrateAllocationHelper::GetRecommendedBitrate(const std::string& target_app,
                                                   const std::string& target_stream,
                                                   const std::string& protocol,
                                                   const std::string& output_protocol,
                                                   const std::string& resolution,
                                                   int fps,
                                                   int priority,
                                                   int source_bitrate_kbps) {
    LOG_INFO("[BitrateAllocationHelper] →→→ ENTRY (not printing params to avoid crash)");
    
    // CRITICAL FIX: If bitrate_allocator is NULL, use intelligent fallback
    if (!bitrate_allocator_) {
        int fallback_bitrate = source_bitrate_kbps > 0 ? source_bitrate_kbps : 2000;
        LOG_WARN("[BitrateAllocationHelper] bitrate_allocator is NULL! Using fallback: {} kbps (source: {}, default: 2000)",
                 fallback_bitrate, source_bitrate_kbps);
        return fallback_bitrate;
    }
    
    LOG_INFO("[BitrateAllocationHelper] A: Getting current time...");
    // 获取创建时间
    auto now = std::chrono::system_clock::now();
    int64_t create_time = std::chrono::duration_cast<std::chrono::seconds>(
        now.time_since_epoch()).count();
    
    LOG_INFO("[BitrateAllocationHelper] B: Calling bitrate_allocator_->GetRecommendedBitrate...");
    // 获取系统推荐的码率
    int recommended_bitrate = bitrate_allocator_->GetRecommendedBitrate(
        target_app,
        target_stream,
        protocol,
        output_protocol,
        resolution,
        fps,
        priority,
        ::utils::StreamAllocationStatus::Starting,
        create_time
    );
    
    LOG_INFO("[BitrateAllocationHelper] C: Got recommended_bitrate={}, processing...", recommended_bitrate);
    
    // 智能码率分配策略（不降级原则）：
    // 如果检测到源流码率，优先使用源流码率（保持质量，不降级）
    int final_bitrate = recommended_bitrate;
    if (source_bitrate_kbps > 0) {
        // 源流码率有效，优先使用源流码率（不降级）
        final_bitrate = source_bitrate_kbps;
        LOG_INFO("[BitrateAllocationHelper] 智能码率分配: {}/{} -> {} kbps (使用源流码率，保持质量，不降级。系统推荐: {} kbps)",
                target_app, target_stream, final_bitrate, recommended_bitrate);
    } else {
        // 未检测到源流码率，使用系统推荐码率
        LOG_INFO("[BitrateAllocationHelper] 智能码率分配: {}/{} -> {} kbps (协议: {}, 输出协议: {}, 分辨率: {}, 帧率: {})",
                target_app, target_stream, recommended_bitrate, protocol, output_protocol, resolution, fps);
    }
    
    LOG_INFO("[BitrateAllocationHelper] ←←← EXIT: returning {}", final_bitrate);
    return final_bitrate;
}

} // namespace utils
} // namespace gateway

