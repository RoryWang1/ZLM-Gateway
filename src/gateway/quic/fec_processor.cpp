#include "gateway/quic/fec_processor.hpp"
#include "utils/logger.hpp"
#include <algorithm>
#include <cstring>

namespace gateway {
namespace quic {

// SMPTE2022_1Decoder 实现
SMPTE2022_1Decoder::SMPTE2022_1Decoder(uint16_t L, uint16_t D)
    : L_(L), D_(D) {
    if (L_ == 0) L_ = 10;  // 默认值
    if (D_ == 0) D_ = 10;  // 默认值
}

bool SMPTE2022_1Decoder::InputPacket(const uint8_t* data, size_t len, 
                                     uint16_t seq_num, bool is_fec) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    FECPacket packet;
    packet.data.assign(data, data + len);
    packet.sequence_number = seq_num;
    packet.is_fec = is_fec;
    
    // 计算列和行索引（简化实现）
    if (is_fec) {
        // FEC 包的序列号通常编码了列/行信息
        // 这里简化处理，实际需要根据协议规范解析
        packet.column_index = seq_num % L_;
        packet.row_index = seq_num / L_;
        fec_packets_[seq_num] = packet;
    } else {
        packet.column_index = seq_num % L_;
        packet.row_index = seq_num / L_;
        media_packets_[seq_num] = packet;
    }
    
    // 尝试恢复丢失的包
    TryRecoverPackets();
    
    return true;
}

bool SMPTE2022_1Decoder::GetRecoveredPacket(uint8_t* output, size_t* len) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    if (recovered_packets_.empty()) {
        return false;
    }
    
    FECPacket packet = recovered_packets_.front();
    recovered_packets_.erase(recovered_packets_.begin());
    
    if (*len < packet.data.size()) {
        *len = packet.data.size();
        return false;  // 缓冲区太小
    }
    
    std::memcpy(output, packet.data.data(), packet.data.size());
    *len = packet.data.size();
    
    return true;
}

void SMPTE2022_1Decoder::Reset() {
    std::lock_guard<std::mutex> lock(mutex_);
    media_packets_.clear();
    fec_packets_.clear();
    recovered_packets_.clear();
}

bool SMPTE2022_1Decoder::TryRecoverPackets() {
    // TODO: 实现完整的 SMPTE 2022-1 恢复算法
    // 这里是一个简化实现
    
    // 检查是否有丢失的媒体包
    // 对于每个丢失的包，尝试使用列 FEC 或行 FEC 恢复
    
    // 简化实现：如果收到 FEC 包且对应的媒体包缺失，尝试恢复
    for (const auto& fec_pair : fec_packets_) {
        const FECPacket& fec_packet = fec_pair.second;
        
        // 检查对应的媒体包是否存在
        // 这里需要根据 FEC 算法确定对应的媒体包序列号
        // 简化处理：假设 FEC 包可以恢复同一列或行的丢失包
        
        // 实际实现需要：
        // 1. 确定 FEC 包对应的媒体包范围
        // 2. 检查哪些媒体包丢失
        // 3. 使用 XOR 或其他算法恢复
    }
    
    return false;
}

std::vector<uint8_t> SMPTE2022_1Decoder::ComputeColumnFEC(uint16_t column_index) {
    // TODO: 实现列 FEC 计算
    return {};
}

std::vector<uint8_t> SMPTE2022_1Decoder::ComputeRowFEC(uint16_t row_index) {
    // TODO: 实现行 FEC 计算
    return {};
}

// FECProcessor 实现
FECProcessor::FECProcessor(FECAlgorithm algorithm, 
                          const std::map<std::string, int>& params)
    : expected_seq_num_(0), seq_num_initialized_(false) {
    
    switch (algorithm) {
        case FECAlgorithm::SMPTE_2022_1: {
            int L = params.count("L") ? params.at("L") : 10;
            int D = params.count("D") ? params.at("D") : 10;
            decoder_ = std::make_unique<SMPTE2022_1Decoder>(L, D);
            break;
        }
        case FECAlgorithm::RaptorQ:
            // TODO: 实现 RaptorQ 解码器
            LOG_WARN("[FECProcessor] RaptorQ 解码器尚未实现，使用 SMPTE 2022-1");
            decoder_ = std::make_unique<SMPTE2022_1Decoder>(10, 10);
            break;
        case FECAlgorithm::ReedSolomon:
            // TODO: 实现 Reed-Solomon 解码器
            LOG_WARN("[FECProcessor] Reed-Solomon 解码器尚未实现，使用 SMPTE 2022-1");
            decoder_ = std::make_unique<SMPTE2022_1Decoder>(10, 10);
            break;
    }
}

void FECProcessor::ProcessQuicData(const uint8_t* data, size_t len) {
    ParseQuicPacket(data, len);
}

void FECProcessor::SetOutputCallback(std::function<void(const uint8_t*, size_t)> callback) {
    std::lock_guard<std::mutex> lock(callback_mutex_);
    output_callback_ = callback;
}

void FECProcessor::Reset() {
    if (decoder_) {
        decoder_->Reset();
    }
    expected_seq_num_ = 0;
    seq_num_initialized_ = false;
}

void FECProcessor::ParseQuicPacket(const uint8_t* data, size_t len) {
    // TODO: 解析 QUIC 数据包格式
    // 实际格式取决于 QUIC 应用层协议设计
    // 这里假设数据包格式为：
    // [序列号(2字节)][FEC标志(1字节)][数据]
    
    if (len < 3) {
        LOG_WARN("[FECProcessor] 数据包太短: {} 字节", len);
        return;
    }
    
    // 解析序列号和 FEC 标志（简化实现）
    uint16_t seq_num = (data[0] << 8) | data[1];
    bool is_fec = (data[2] & 0x01) != 0;
    const uint8_t* packet_data = data + 3;
    size_t packet_len = len - 3;
    
    // 输入到 FEC 解码器
    if (decoder_) {
        decoder_->InputPacket(packet_data, packet_len, seq_num, is_fec);
        
        // 尝试获取恢复的包
        uint8_t recovered_buffer[188];  // TS 包大小
        size_t recovered_len = sizeof(recovered_buffer);
        
        while (decoder_->GetRecoveredPacket(recovered_buffer, &recovered_len)) {
            // 调用输出回调
            std::lock_guard<std::mutex> lock(callback_mutex_);
            if (output_callback_) {
                output_callback_(recovered_buffer, recovered_len);
            }
            recovered_len = sizeof(recovered_buffer);
        }
        
        // 如果这不是 FEC 包，直接输出
        if (!is_fec) {
            std::lock_guard<std::mutex> lock(callback_mutex_);
            if (output_callback_) {
                output_callback_(packet_data, packet_len);
            }
        }
    }
}

} // namespace quic
} // namespace gateway


