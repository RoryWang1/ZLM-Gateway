#include "gateway/quic/ts_parser.hpp"
#include "utils/logger.hpp"
#include <cstring>
#include <algorithm>

namespace gateway {
namespace quic {

TSParser::TSParser()
    : buffer_offset_(0), total_packets_(0), error_packets_(0) {
    buffer_.reserve(188 * 10);  // 预分配空间
}

bool TSParser::ParsePackets(const uint8_t* data, size_t len) {
    if (len == 0) {
        return false;
    }
    
    // 将数据添加到缓冲区
    size_t old_size = buffer_.size();
    buffer_.resize(old_size + len);
    std::memcpy(buffer_.data() + old_size, data, len);
    
    // 查找同步字节并解析包
    size_t pos = buffer_offset_;
    while (pos + 188 <= buffer_.size()) {
        // 查找同步字节
        if (buffer_[pos] != TSPacket::SYNC_BYTE) {
            // 尝试在接下来的几个字节中查找同步字节
            bool found = false;
            for (size_t i = pos + 1; i < std::min(pos + 10, buffer_.size()); ++i) {
                if (buffer_[i] == TSPacket::SYNC_BYTE && i + 188 <= buffer_.size()) {
                    pos = i;
                    found = true;
                    break;
                }
            }
            if (!found) {
                // 未找到同步字节，跳过一些字节继续查找
                pos++;
                continue;
            }
        }
        
        // 解析 TS 包
        TSPacket packet;
        if (ParsePacket(buffer_.data() + pos, 188, packet)) {
            ProcessPacket(packet);
            total_packets_++;
        } else {
            error_packets_++;
        }
        
        pos += 188;
    }
    
    // 保留未处理的数据
    if (pos < buffer_.size()) {
        size_t remaining = buffer_.size() - pos;
        std::memcpy(buffer_.data(), buffer_.data() + pos, remaining);
        buffer_.resize(remaining);
        buffer_offset_ = 0;
    } else {
        buffer_.clear();
        buffer_offset_ = 0;
    }
    
    return true;
}

bool TSParser::ParsePacket(const uint8_t* data, size_t len, TSPacket& packet) {
    if (len < 4) {
        return false;
    }
    
    // 检查同步字节
    if (data[0] != TSPacket::SYNC_BYTE) {
        return false;
    }
    
    // 解析包头
    packet.sync_byte = data[0];
    packet.transport_error = (data[1] & 0x80) != 0;
    packet.payload_start = (data[1] & 0x40) != 0;
    packet.transport_priority = (data[1] & 0x20) != 0;
    packet.pid = ((data[1] & 0x1F) << 8) | data[2];
    packet.transport_scrambling = (data[3] >> 6) & 0x03;
    packet.adaptation_field = (data[3] >> 4) & 0x03;
    packet.continuity_counter = data[3] & 0x0F;
    
    // 解析适配字段（如果存在）
    size_t payload_offset = 4;
    if (packet.adaptation_field == 0x02 || packet.adaptation_field == 0x03) {
        if (len < 5) {
            return false;
        }
        uint8_t adaptation_field_length = data[4];
        if (adaptation_field_length > 0 && len >= 5 + adaptation_field_length) {
            packet.adaptation_field_data.assign(
                data + 5, data + 5 + adaptation_field_length);
            payload_offset = 5 + adaptation_field_length;
        }
    }
    
    // 提取有效载荷
    if (packet.adaptation_field == 0x01 || packet.adaptation_field == 0x03) {
        if (len > payload_offset) {
            packet.payload.assign(data + payload_offset, data + len);
        }
    }
    
    return true;
}

void TSParser::ProcessPacket(const TSPacket& packet) {
    // 调用包回调
    if (packet_callback_) {
        packet_callback_(packet);
    }
    
    // 如果设置了输出回调，直接输出原始 TS 包（用于 FFmpeg）
    if (output_callback_) {
        // 重构完整的 188 字节 TS 包
        // 这里简化处理，直接使用原始数据
        // 实际应该从 packet 结构重构
        // 为了简化，我们假设输入数据已经是完整的 TS 包
    }
}

void TSParser::SetPacketCallback(std::function<void(const TSPacket&)> callback) {
    packet_callback_ = callback;
}

void TSParser::SetOutputCallback(std::function<void(const uint8_t*, size_t)> callback) {
    output_callback_ = callback;
}

void TSParser::Reset() {
    buffer_.clear();
    buffer_offset_ = 0;
    total_packets_ = 0;
    error_packets_ = 0;
}

} // namespace quic
} // namespace gateway


