#ifndef GATEWAY_QUIC_TS_PARSER_HPP
#define GATEWAY_QUIC_TS_PARSER_HPP

#include <cstdint>
#include <cstddef>
#include <functional>
#include <vector>
#include <memory>

namespace gateway {
namespace quic {

/**
 * @brief TS 包结构
 */
struct TSPacket {
    static constexpr uint8_t SYNC_BYTE = 0x47;
    
    uint8_t sync_byte;           // 同步字节 (0x47)
    bool transport_error;        // 传输错误指示
    bool payload_start;          // 有效载荷单元起始指示
    bool transport_priority;      // 传输优先级
    uint16_t pid;                // 包标识符 (13 bits)
    uint8_t transport_scrambling; // 传输加扰控制 (2 bits)
    uint8_t adaptation_field;    // 适配字段控制 (2 bits)
    uint8_t continuity_counter;  // 连续性计数器 (4 bits)
    
    std::vector<uint8_t> payload;  // 有效载荷
    std::vector<uint8_t> adaptation_field_data;  // 适配字段数据
};

/**
 * @brief TS 解析器
 * 
 * 解析 MPEG-TS 流，提取音视频数据
 */
class TSParser {
public:
    TSParser();
    ~TSParser() = default;
    
    /**
     * @brief 解析 TS 包数据
     * @param data 数据
     * @param len 长度
     * @return 是否成功
     */
    bool ParsePackets(const uint8_t* data, size_t len);
    
    /**
     * @brief 设置 TS 包回调
     * @param callback 回调函数，参数为 TSPacket
     */
    void SetPacketCallback(std::function<void(const TSPacket&)> callback);
    
    /**
     * @brief 设置输出回调（用于直接输出到 FFmpeg）
     * @param callback 回调函数，参数为 (data, len)
     */
    void SetOutputCallback(std::function<void(const uint8_t*, size_t)> callback);
    
    /**
     * @brief 重置解析器
     */
    void Reset();

private:
    /**
     * @brief 解析单个 TS 包
     * @param data 数据（188 字节）
     * @param len 长度
     * @param packet 输出的包结构
     * @return 是否成功
     */
    bool ParsePacket(const uint8_t* data, size_t len, TSPacket& packet);
    
    /**
     * @brief 处理 TS 包
     * @param packet TS 包
     */
    void ProcessPacket(const TSPacket& packet);
    
    std::function<void(const TSPacket&)> packet_callback_;
    std::function<void(const uint8_t*, size_t)> output_callback_;
    
    // 缓冲区用于处理不完整的包
    std::vector<uint8_t> buffer_;
    size_t buffer_offset_;
    
    // 统计信息
    uint64_t total_packets_;
    uint64_t error_packets_;
};

} // namespace quic
} // namespace gateway

#endif // GATEWAY_QUIC_TS_PARSER_HPP


