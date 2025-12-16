#ifndef GATEWAY_UTILS_BITRATE_ALLOCATION_HELPER_HPP
#define GATEWAY_UTILS_BITRATE_ALLOCATION_HELPER_HPP

#include <string>
#include <memory>

namespace utils {
class BitrateAllocator;
}

namespace gateway {
namespace utils {

/**
 * @brief 码率分配辅助类
 * 
 * 封装所有 Gateway 中重复的码率分配逻辑
 */
class BitrateAllocationHelper {
public:
    /**
     * @brief 构造函数
     * @param bitrate_allocator 码率分配器（可选）
     */
    explicit BitrateAllocationHelper(std::shared_ptr<::utils::BitrateAllocator> bitrate_allocator = nullptr);

    /**
     * @brief 获取推荐的码率（智能版本，考虑源流码率）
     * 
     * @param target_app 目标应用名
     * @param target_stream 目标流名
     * @param protocol 输入协议（rtsp, http-flv, hls, rtmp 等）
     * @param output_protocol 输出协议（webrtc, http-flv, hls 等）
     * @param resolution 分辨率（如 "1920x1080"）
     * @param fps 帧率
     * @param priority 优先级（默认5）
     * @param source_bitrate_kbps 源流码率（kbps，0表示未检测到）
     * @return 推荐的码率（kbps），如果分配器不存在或失败返回0
     * 
     * 智能码率分配策略（不降级原则）：
     * - 如果源流码率 > 0：优先使用源流码率（保持质量，不降级）
     * - 如果源流码率 = 0：使用系统推荐的码率
     */
    int GetRecommendedBitrate(const std::string& target_app,
                             const std::string& target_stream,
                             const std::string& protocol,
                             const std::string& output_protocol,
                             const std::string& resolution = "1280x720",
                             int fps = 30,
                             int priority = 5,
                             int source_bitrate_kbps = 0);

    /**
     * @brief 检查是否有码率分配器
     * @return 是否有码率分配器
     */
    bool HasAllocator() const { return bitrate_allocator_ != nullptr; }

private:
    std::shared_ptr<::utils::BitrateAllocator> bitrate_allocator_;
};

} // namespace utils
} // namespace gateway

#endif // GATEWAY_UTILS_BITRATE_ALLOCATION_HELPER_HPP

