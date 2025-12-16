#ifndef GATEWAY_QUIC_FEC_PROCESSOR_HPP
#define GATEWAY_QUIC_FEC_PROCESSOR_HPP

#include <cstdint>
#include <cstddef>
#include <functional>
#include <memory>
#include <map>
#include <vector>
#include <mutex>

namespace gateway {
namespace quic {

/**
 * @brief FEC 算法类型
 */
enum class FECAlgorithm {
    SMPTE_2022_1,  // SMPTE 2022-1 (Pro-MPEG Code of Practice #3)
    RaptorQ,       // RFC 6330 RaptorQ
    ReedSolomon    // Reed-Solomon
};

/**
 * @brief FEC 数据包
 */
struct FECPacket {
    std::vector<uint8_t> data;
    uint16_t sequence_number;
    bool is_fec;  // true 表示 FEC 包，false 表示媒体包
    uint16_t column_index;  // 列索引（用于 2D FEC）
    uint16_t row_index;     // 行索引（用于 2D FEC）
};

/**
 * @brief FEC 处理器基类
 */
class FECDecoder {
public:
    virtual ~FECDecoder() = default;
    
    /**
     * @brief 输入 FEC 保护的数据包
     * @param data 数据
     * @param len 长度
     * @param seq_num 序列号
     * @param is_fec 是否为 FEC 包
     * @return 是否成功
     */
    virtual bool InputPacket(const uint8_t* data, size_t len, 
                            uint16_t seq_num, bool is_fec) = 0;
    
    /**
     * @brief 获取恢复的数据包
     * @param output 输出缓冲区
     * @param len 输出长度（输入时为缓冲区大小，输出时为实际长度）
     * @return 是否成功恢复
     */
    virtual bool GetRecoveredPacket(uint8_t* output, size_t* len) = 0;
    
    /**
     * @brief 重置解码器
     */
    virtual void Reset() = 0;
};

/**
 * @brief SMPTE 2022-1 FEC 解码器
 * 
 * 实现 Pro-MPEG Code of Practice #3 标准的 2D 奇偶校验 FEC
 */
class SMPTE2022_1Decoder : public FECDecoder {
public:
    /**
     * @brief 构造函数
     * @param L 列数（Column FEC）
     * @param D 行数（Row FEC）
     */
    SMPTE2022_1Decoder(uint16_t L, uint16_t D);
    
    ~SMPTE2022_1Decoder() override = default;
    
    bool InputPacket(const uint8_t* data, size_t len, 
                    uint16_t seq_num, bool is_fec) override;
    
    bool GetRecoveredPacket(uint8_t* output, size_t* len) override;
    
    void Reset() override;

private:
    /**
     * @brief 尝试恢复丢失的包
     * @return 是否成功恢复
     */
    bool TryRecoverPackets();
    
    /**
     * @brief 计算列 FEC
     * @param column_index 列索引
     * @return 恢复的数据包
     */
    std::vector<uint8_t> ComputeColumnFEC(uint16_t column_index);
    
    /**
     * @brief 计算行 FEC
     * @param row_index 行索引
     * @return 恢复的数据包
     */
    std::vector<uint8_t> ComputeRowFEC(uint16_t row_index);
    
    uint16_t L_;  // 列数
    uint16_t D_;  // 行数
    
    // 媒体包缓冲区：seq_num -> packet
    std::map<uint16_t, FECPacket> media_packets_;
    
    // FEC 包缓冲区：seq_num -> packet
    std::map<uint16_t, FECPacket> fec_packets_;
    
    // 恢复的数据包队列
    std::vector<FECPacket> recovered_packets_;
    
    std::mutex mutex_;
};

/**
 * @brief FEC 处理器
 * 
 * 处理来自 QUIC 流的数据，进行 FEC 解码，输出恢复的 TS 包
 */
class FECProcessor {
public:
    /**
     * @brief 构造函数
     * @param algorithm FEC 算法
     * @param params 算法参数（如 L, D for SMPTE 2022-1）
     */
    FECProcessor(FECAlgorithm algorithm, 
                const std::map<std::string, int>& params = {});
    
    ~FECProcessor() = default;
    
    /**
     * @brief 处理来自 QUIC 流的数据
     * @param data 数据
     * @param len 长度
     */
    void ProcessQuicData(const uint8_t* data, size_t len);
    
    /**
     * @brief 设置输出回调（恢复的 TS 包）
     * @param callback 回调函数，参数为 (data, len)
     */
    void SetOutputCallback(std::function<void(const uint8_t*, size_t)> callback);
    
    /**
     * @brief 重置处理器
     */
    void Reset();

private:
    /**
     * @brief 解析 QUIC 数据包，提取 FEC 保护的包
     * @param data 数据
     * @param len 长度
     */
    void ParseQuicPacket(const uint8_t* data, size_t len);
    
    std::unique_ptr<FECDecoder> decoder_;
    std::function<void(const uint8_t*, size_t)> output_callback_;
    std::mutex callback_mutex_;
    
    // 序列号跟踪
    uint16_t expected_seq_num_;
    bool seq_num_initialized_;
};

} // namespace quic
} // namespace gateway

#endif // GATEWAY_QUIC_FEC_PROCESSOR_HPP


