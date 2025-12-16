#include "gateway/gb28181/gb28181_stream_matcher.hpp"
#include "utils/logger.hpp"
#include <algorithm>
#include <cctype>

namespace gateway {
namespace gb28181 {

// ============================================================================
// StreamKey Implementation
// ============================================================================

StreamKey::StreamKey(const std::string& stream) 
    : full_name(stream), timestamp(0) {
    
    // GB28181流名称格式: deviceId_channelId_timestamp
    // 例如: 34020000001320000001_34020000001320000001_20231213140530
    
    // 查找最后一个下划线
    size_t last_underscore = stream.find_last_of('_');
    
    if (last_underscore == std::string::npos || last_underscore >= stream.length() - 1) {
        // 没有下划线或下划线在末尾，使用完整名称作为base_name
        base_name = stream;
        return;
    }
    
    // 提取可能的时间戳部分
    std::string possible_timestamp = stream.substr(last_underscore + 1);
    
    // 检查是否是纯数字且长度>=10（时间戳特征）
    bool is_timestamp = !possible_timestamp.empty() && 
                       possible_timestamp.length() >= 10 &&
                       std::all_of(possible_timestamp.begin(), 
                                  possible_timestamp.end(), 
                                  ::isdigit);
    
    if (is_timestamp) {
        // 提取base_name（去除时间戳）
        base_name = stream.substr(0, last_underscore);
        
        // 解析时间戳
        try {
            timestamp = std::stoll(possible_timestamp);
        } catch (...) {
            timestamp = 0;
        }
    } else {
        // 不是时间戳格式，使用完整名称
        base_name = stream;
    }
}

// ============================================================================
// GB28181StreamMatcher Implementation
// ============================================================================

const StreamKey& GB28181StreamMatcher::GetKey(const std::string& stream) {
    // 先尝试读锁快速查找
    {
        std::lock_guard<std::mutex> lock(cache_mutex_);
        auto it = cache_.find(stream);
        if (it != cache_.end()) {
            cache_hits_++;
            return it->second;
        }
    }
    
    // 未命中，需要解析并缓存
    cache_misses_++;
    
    StreamKey key(stream);
    
    // 写锁插入缓存
    std::lock_guard<std::mutex> lock(cache_mutex_);
    
    // 双重检查（防止并发插入）
    auto it = cache_.find(stream);
    if (it != cache_.end()) {
        return it->second;
    }
    
    // 插入新key
    auto result = cache_.emplace(stream, std::move(key));
    return result.first->second;
}

bool GB28181StreamMatcher::Match(const std::string& stream1, 
                                 const std::string& stream2) {
    // 快速路径：完全相同
    if (stream1 == stream2) {
        return true;
    }
    
    // 获取两个流的key并比较base_name
    const auto& key1 = GetKey(stream1);
    const auto& key2 = GetKey(stream2);
    
    return key1.base_name == key2.base_name;
}

void GB28181StreamMatcher::InvalidateCache(const std::string& stream) {
    std::lock_guard<std::mutex> lock(cache_mutex_);
    cache_.erase(stream);
}

void GB28181StreamMatcher::ClearCache() {
    std::lock_guard<std::mutex> lock(cache_mutex_);
    cache_.clear();
    cache_hits_ = 0;
    cache_misses_ = 0;
    
    LOG_DEBUG("GB28181StreamMatcher: Cache cleared");
}

size_t GB28181StreamMatcher::CacheSize() const {
    std::lock_guard<std::mutex> lock(cache_mutex_);
    return cache_.size();
}

} // namespace gb28181
} // namespace gateway
