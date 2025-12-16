#ifndef UTILS_BITRATE_ALLOCATOR_HPP
#define UTILS_BITRATE_ALLOCATOR_HPP

#include <string>
#include <vector>
#include <map>
#include <memory>
#include <mutex>
#include "streaming/zlmediakit/zlm_client.hpp"

namespace streaming {
class StreamManager;
}

namespace utils {

/**
 * @brief 流状态（用于码率分配）
 */
enum class StreamAllocationStatus {
    Starting,   // 启动中：给最小码率，快速启动
    Running,    // 运行中：正常分配
    Stopping,   // 停止中：降低码率
    Error       // 错误：不给码率
};

/**
 * @brief 流码率分配信息
 */
struct StreamBitrateInfo {
    std::string app;
    std::string stream;
    std::string protocol;
    std::string output_protocol;
    int reader_count = 0;           // 观看者数量
    int64_t current_bytes_speed = 0; // 当前传输速度（字节/秒）
    int current_bitrate_kbps = 0;   // 当前码率（kbps）
    int priority = 5;                // 优先级（1-10，10最高）
    std::string resolution;         // 分辨率（如 "1920x1080"）
    int fps = 30;                    // 帧率
    bool is_active = false;         // 是否活跃
    StreamAllocationStatus status = StreamAllocationStatus::Running; // 流状态
    int64_t create_time = 0;        // 创建时间（Unix时间戳）
    int64_t running_duration_sec = 0; // 运行时长（秒）
    double stability_score = 1.0;   // 稳定性评分（0-1，1最稳定）
    int consecutive_errors = 0;      // 连续错误次数
    double network_quality = 1.0;   // 网络质量（0-1，1最好，基于丢包率和延迟）
    bool requires_low_latency = false; // 是否需要低延迟（WebRTC等）
    int min_quality_bitrate = 0;    // 最低质量要求的码率（kbps）
    int preferred_bitrate = 0;      // 偏好码率（kbps，如果带宽充足）
};

/**
 * @brief 码率分配配置
 */
struct BitrateAllocatorConfig {
    int total_bandwidth_mbps = 100;     // 总带宽（Mbps）
    int min_bitrate_kbps = 500;          // 最小码率（kbps）
    int max_bitrate_kbps = 8000;         // 最大码率（kbps）
    int default_bitrate_kbps = 2000;     // 默认码率（kbps）
    int reserved_bandwidth_mbps = 10;    // 保留带宽（Mbps），用于系统开销
    bool enable_adaptive = true;         // 是否启用自适应码率
    int update_interval_sec = 10;         // 更新间隔（秒）
    
    // 智能分配参数
    double starting_bitrate_factor = 0.5;  // 启动中流的码率因子（50%）
    double stopping_bitrate_factor = 0.3; // 停止中流的码率因子（30%）
    int min_stable_duration_sec = 30;      // 最小稳定运行时长（秒），超过此时间才认为稳定
    double stability_weight = 0.2;          // 稳定性权重（0-1）
    double network_quality_weight = 0.15;  // 网络质量权重（0-1）
    bool enable_protocol_optimization = true; // 是否启用协议优化（WebRTC低延迟等）
    int max_concurrent_starting_streams = 3; // 最大并发启动流数量
};

/**
 * @brief 码率分配器
 * 
 * 根据当前所有活跃流动态分配码率，确保所有流都流畅清晰
 */
class BitrateAllocator {
public:
    /**
     * @brief 构造函数
     * @param config 配置对象
     * @param zlm_client ZLMediaKit客户端（用于获取流信息）
     */
    BitrateAllocator(const BitrateAllocatorConfig& config,
                     std::shared_ptr<streaming::ZLMClient> zlm_client);

    /**
     * @brief 析构函数
     */
    ~BitrateAllocator();

    /**
     * @brief 获取流的推荐码率（智能版本）
     * @param app 应用名
     * @param stream 流名
     * @param protocol 协议类型
     * @param output_protocol 输出协议
     * @param resolution 分辨率（可选，如 "1920x1080"）
     * @param fps 帧率（可选，默认30）
     * @param priority 优先级（1-10，10最高，默认5）
     * @param status 流状态（可选，默认Running）
     * @param create_time 创建时间（可选，Unix时间戳）
     * @return 推荐码率（kbps）
     */
    int GetRecommendedBitrate(const std::string& app,
                              const std::string& stream,
                              const std::string& protocol,
                              const std::string& output_protocol,
                              const std::string& resolution = "",
                              int fps = 30,
                              int priority = 5,
                              StreamAllocationStatus status = StreamAllocationStatus::Running,
                              int64_t create_time = 0);

    /**
     * @brief 更新所有流的码率分配（智能版本）
     * @param stream_manager 流管理器（可选，用于获取流元数据）
     * @return 是否成功
     */
    bool UpdateAllocations(std::shared_ptr<streaming::StreamManager> stream_manager = nullptr);

    /**
     * @brief 获取流的码率信息
     * @param app 应用名
     * @param stream 流名
     * @return 码率信息，不存在返回nullptr
     */
    std::shared_ptr<StreamBitrateInfo> GetStreamInfo(const std::string& app,
                                                     const std::string& stream);

    /**
     * @brief 获取所有流的码率分配
     * @return 流码率信息列表
     */
    std::vector<StreamBitrateInfo> GetAllAllocations();

    /**
     * @brief 设置配置
     * @param config 新配置
     */
    void SetConfig(const BitrateAllocatorConfig& config);

    /**
     * @brief 获取配置
     * @return 当前配置
     */
    BitrateAllocatorConfig GetConfig() const;

private:
    /**
     * @brief Internal unlocked version of UpdateAllocations
     * @note MUST be called with mutex_ already held
     * @param stream_manager 流管理器（可选，用于获取流元数据）
     * @return 是否成功
     */
    bool UpdateAllocationsUnlocked(std::shared_ptr<streaming::StreamManager> stream_manager = nullptr);
    
    /**
     * @brief 从ZLMediaKit获取所有活跃流
     * @return 流信息列表
     */
    std::vector<streaming::StreamInfo> FetchActiveStreams();

    /**
     * @brief 计算流的推荐码率（基于分辨率和帧率）
     * @param resolution 分辨率（如 "1920x1080"）
     * @param fps 帧率
     * @return 基础码率（kbps）
     */
    int CalculateBaseBitrate(const std::string& resolution, int fps);

    /**
     * @brief 根据优先级调整码率
     * @param base_bitrate 基础码率（kbps）
     * @param priority 优先级（1-10）
     * @return 调整后的码率（kbps）
     */
    int AdjustBitrateByPriority(int base_bitrate, int priority);

    /**
     * @brief 根据观看者数量调整码率
     * @param base_bitrate 基础码率（kbps）
     * @param reader_count 观看者数量
     * @return 调整后的码率（kbps）
     */
    int AdjustBitrateByViewers(int base_bitrate, int reader_count);

    /**
     * @brief 根据流状态调整码率
     * @param base_bitrate 基础码率（kbps）
     * @param status 流状态
     * @return 调整后的码率（kbps）
     */
    int AdjustBitrateByStatus(int base_bitrate, StreamAllocationStatus status);

    /**
     * @brief 根据输出协议调整码率需求
     * @param base_bitrate 基础码率（kbps）
     * @param output_protocol 输出协议
     * @return 调整后的码率（kbps）
     */
    int AdjustBitrateByProtocol(int base_bitrate, const std::string& output_protocol);

    /**
     * @brief 根据流稳定性调整码率
     * @param base_bitrate 基础码率（kbps）
     * @param stability_score 稳定性评分（0-1）
     * @param running_duration_sec 运行时长（秒）
     * @return 调整后的码率（kbps）
     */
    int AdjustBitrateByStability(int base_bitrate, double stability_score, int64_t running_duration_sec);

    /**
     * @brief 根据网络质量调整码率
     * @param base_bitrate 基础码率（kbps）
     * @param network_quality 网络质量（0-1）
     * @return 调整后的码率（kbps）
     */
    int AdjustBitrateByNetworkQuality(int base_bitrate, double network_quality);

    /**
     * @brief 计算流的稳定性评分
     * @param stream_info 流信息
     * @return 稳定性评分（0-1）
     */
    double CalculateStabilityScore(const StreamBitrateInfo& stream_info);

    /**
     * @brief 计算网络质量评分
     * @param stream_info 流信息
     * @return 网络质量评分（0-1）
     */
    double CalculateNetworkQuality(const StreamBitrateInfo& stream_info);

    /**
     * @brief 根据流数量自适应调整总带宽
     * @param stream_count 流数量
     * @return 调整后的可用带宽（kbps）
     */
    int AdjustTotalBandwidthByStreamCount(size_t stream_count);

    /**
     * @brief 分配码率给所有流（智能版本）
     * @param streams 流信息列表
     * @param total_bandwidth_kbps 总可用带宽（kbps）
     * @return 分配结果（流名 -> 码率）
     */
    std::map<std::string, int> AllocateBitrates(
        const std::vector<StreamBitrateInfo>& streams,
        int total_bandwidth_kbps);

    BitrateAllocatorConfig config_;
    std::shared_ptr<streaming::ZLMClient> zlm_client_;
    std::map<std::string, std::shared_ptr<StreamBitrateInfo>> stream_infos_;
    mutable std::mutex mutex_;  // mutable，允许在const方法中加锁
    int64_t last_update_time_ = 0;
};

} // namespace utils

#endif // UTILS_BITRATE_ALLOCATOR_HPP

