#pragma once

#include <string>
#include <unordered_map>
#include <mutex>
#include <atomic>
#include <cstdint>

namespace gateway {
namespace gb28181 {

/**
 * @brief GB28181流名称解析结果
 * 
 * GB28181流名称格式: {deviceId}_{channelId}_YYYYMMDDHHMMSS
 * 例如: 34020000001320000001_34020000001320000001_20231213140530
 */
struct StreamKey {
    std::string base_name;      // 去除时间戳后的基础名称 (deviceId_channelId)
    std::string full_name;      // 原始完整名称
    int64_t timestamp;          // 解析出的时间戳 (YYYYMMDDHHMMSS)
    
    /**
     * @brief 从流名称解析StreamKey
     * @param stream 流名称
     */
    explicit StreamKey(const std::string& stream);
    
    /**
     * @brief 检查是否包含时间戳
     */
    bool HasTimestamp() const { return timestamp > 0; }
};

/**
 * @brief GB28181流名称匹配器（带缓存）
 * 
 * 用于高效匹配GB28181流名称。通过缓存解析结果，将O(N×M)的匹配
 * 复杂度降低到O(1)。
 * 
 * 线程安全：使用std::mutex保护
 */
class GB28181StreamMatcher {
public:
    GB28181StreamMatcher() = default;
    ~GB28181StreamMatcher() = default;
    
    // 禁止拷贝和移动
    GB28181StreamMatcher(const GB28181StreamMatcher&) = delete;
    GB28181StreamMatcher& operator=(const GB28181StreamMatcher&) = delete;
    GB28181StreamMatcher(GB28181StreamMatcher&&) = delete;
    GB28181StreamMatcher& operator=(GB28181StreamMatcher&&) = delete;
    
    /**
     * @brief 获取流的StreamKey（带缓存）
     * @param stream 流名称
     * @return StreamKey引用
     * 
     * 首次调用会解析并缓存，后续调用直接返回缓存结果。
     */
    const StreamKey& GetKey(const std::string& stream);
    
    /**
     * @brief 匹配两个流名称
     * @param stream1 流名称1
     * @param stream2 流名称2
     * @return 是否匹配（基础名称相同）
     * 
     * 匹配规则：
     * - 34020000001320000001_34020000001320000001_20231213140530
     * - 34020000001320000001_34020000001320000001_202312131406 00
     * 上述两个流名称会匹配（基础名称相同）
     */
    bool Match(const std::string& stream1, const std::string& stream2);
    
    /**
     * @brief 使指定流的缓存失效
     * @param stream 流名称
     */
    void InvalidateCache(const std::string& stream);
    
    /**
     * @brief 清空所有缓存
     */
    void ClearCache();
    
    /**
     * @brief 获取缓存大小
     */
    size_t CacheSize() const;
    
    /**
     * @brief 获取缓存命中次数
     */
    size_t CacheHits() const { return cache_hits_.load(); }
    
    /**
     * @brief 获取缓存未命中次数
     */
    size_t CacheMisses() const { return cache_misses_.load(); }
    
    /**
     * @brief 获取缓存命中率
     */
    double CacheHitRate() const {
        size_t total = cache_hits_.load() + cache_misses_.load();
        return total > 0 ? static_cast<double>(cache_hits_.load()) / total : 0.0;
    }
    
private:
    // 缓存：流名称 → StreamKey
    std::unordered_map<std::string, StreamKey> cache_;
    mutable std::mutex cache_mutex_;
    
    // 统计信息
    std::atomic<size_t> cache_hits_{0};
    std::atomic<size_t> cache_misses_{0};
};

} // namespace gb28181
} // namespace gateway
