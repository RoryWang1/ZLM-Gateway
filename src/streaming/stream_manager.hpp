#ifndef STREAMING_STREAM_MANAGER_HPP
#define STREAMING_STREAM_MANAGER_HPP

#include "config/constants.hpp"
#include "gateway/gb28181/gb28181_stream_matcher.hpp"
#include "streaming/zlmediakit/zlm_client.hpp"
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <set>
#include <shared_mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

namespace api {
class WebSocketServer;
}

namespace streaming {

/**
 * @brief 流状态
 */
enum class StreamStatus {
  Stopped,  // 已停止
  Starting, // 启动中
  Running,  // 运行中
  Stopping, // 停止中
  Error     // 错误
};

/**
 * @brief 流元数据
 */
struct StreamMetadata {
  std::string app;      // 应用名
  std::string stream;   // 流名
  std::string protocol; // 协议类型: "rtsp", "rtmp", "http-flv", "onvif", etc.
  std::string output_protocol; // 输出协议: "http-flv", "hls", "webrtc"
  std::string source_url;      // 源流地址
  std::string device_id;       // 设备 ID（如果是设备发现协议）
  std::string device_type;     // 设备类型: "onvif", "isapi", "dahua", etc.
  StreamStatus status;         // 流状态
  int64_t create_time;         // 创建时间（Unix 时间戳，秒）
  int64_t last_update_time;    // 最后更新时间（Unix 时间戳，秒）
  int pid = 0;                 // 进程 ID（如果是 FFmpeg 进程）
  std::string gateway_type;    // Gateway 类型: "native", "rtsp_gateway",
                               // "onvif_gateway", etc.
  std::string processing_type; // 处理类型: "0"(Direct Proxy), "1"(FFmpeg Copy),
                               // "2"(FFmpeg Transcode)
  std::string transcoding_reason; // 转码原因（如果是 FFmpeg Transcode）

  // 从 ZLMediaKit 同步的状态信息（可选）
  bool zlm_alive = false; // ZLMediaKit 中的流是否存活
  int64_t zlm_last_alive_time =
      0; // ZLMediaKit 中流最后一次存活的时间（Unix 时间戳，秒）
  int reader_count = 0;    // 当前观看者数量
  int64_t bytes_speed = 0; // 字节速度
  int64_t total_bytes = 0; // 总字节数
  std::string zlm_app; // ZLM中实际注册的app名称（可能不同于metadata.app）
  std::string
      zlm_stream; // ZLM中实际注册的stream名称（可能包含时间戳，不同于metadata.stream）

  // 错误信息（用于错误模型统一）
  std::string
      error_code; // 错误码（如 "SOURCE_NOT_FOUND", "CODEC_UNSUPPORTED"）
  std::string error_message; // 错误消息（简短文本，便于前端展示）

  // WebSocket 广播控制
  int64_t last_broadcast_time =
      0; // 最后一次WebSocket广播时间（Unix 时间戳，毫秒）
};

/**
 * @brief 流管理器
 *
 * 统一管理所有流的元数据，包括：
 * - 原生协议流（通过 ZLMediaKit API 管理）
 * - 非原生协议流（通过 Gateway + FFmpeg 管理）
 * - 设备发现协议流（通过设备发现 Gateway 管理）
 */
class StreamManager {
public:
  /**
   * @brief 构造函数
   * @param zlm_client ZLMediaKit 客户端（用于状态同步）
   * @param websocket_server WebSocket 服务器（用于实时推送，可选）
   */
  StreamManager(
      std::shared_ptr<ZLMClient> zlm_client,
      std::shared_ptr<api::WebSocketServer> websocket_server = nullptr);

  /**
   * @brief 析构函数
   */
  ~StreamManager();

  /**
   * @brief 注册流
   * @param metadata 流元数据
   * @return 是否成功
   */
  bool RegisterStream(const StreamMetadata &metadata);

  /**
   * @brief Context-free protocol inference from source URL (Phase 2.1)
   * @param source_url The source URL to analyze
   * @return Inferred protocol (e.g. "rtsp", "rtmp", "hls") or empty string if
   * unknown
   */
  static std::string InferProtocolFromSourceUrl(const std::string &source_url);

  /**
   * @brief 注销流
   * @param app 应用名
   * @param stream 流名
   * @return 是否成功
   */
  bool UnregisterStream(const std::string &app, const std::string &stream);

  /**
   * @brief 更新流状态（运维/修复通道）
   *
   * @warning 此接口为运维/修复通道，用于强制覆盖状态，不遵循标准状态转移规则。
   * 正常情况下应使用以下接口：
   * - OnStreamCreateRequested: 创建请求发起
   * - OnStreamCreateResult: 创建结果报告
   * - OnZLMStreamState: ZLM 状态更新
   *
   * 仅在以下场景使用此接口：
   * - 运维手动修复状态不一致
   * - 紧急错误处理（如停止失败后强制标记为 Error）
   *
   * @param app 应用名
   * @param stream 流名
   * @param status 新状态
   * @return 是否成功
   */
  bool UpdateStreamStatus(const std::string &app, const std::string &stream,
                          StreamStatus status);

  /**
   * @brief 更新流元数据
   * @param app 应用名
   * @param stream 流名
   * @param metadata 新的元数据（只更新提供的字段）
   * @return 是否成功
   */
  bool UpdateStreamMetadata(const std::string &app, const std::string &stream,
                            const StreamMetadata &metadata);

  /**
   * @brief 获取流元数据
   * @param app 应用名
   * @param stream 流名
   * @return 流元数据，不存在返回空对象（status == Stopped）
   */
  StreamMetadata GetStreamMetadata(const std::string &app,
                                   const std::string &stream) const;

  /**
   * @brief 遍历所有流（零拷贝，线程安全）
   * @param callback 回调函数，返回 false 停止遍历
   *
   * Phase 1.3 内存优化：替代 GetAllStreams，避免在大并发时产生的巨大内存开销
   */
  void
  ForEachStream(std::function<bool(const StreamMetadata &)> callback) const;

  /**
   * @brief 获取所有流的元数据副本
   * @deprecated 建议使用 ForEachStream 以减少内存开销
   * @return 流元数据列表
   */
  std::vector<StreamMetadata> GetAllStreams() const;

  /**
   * @brief 获取指定协议的流列表
   * @param protocol 协议类型
   * @return 流元数据列表
   */
  std::vector<StreamMetadata>
  GetStreamsByProtocol(const std::string &protocol) const;

  /**
   * @brief 获取指定 Gateway 类型的流列表
   * @param gateway_type Gateway 类型
   * @return 流元数据列表
   */
  std::vector<StreamMetadata>
  GetStreamsByGatewayType(const std::string &gateway_type) const;

  /**
   * @brief 检查流是否存在
   * @param app 应用名
   * @param stream 流名
   * @return 是否存在
   */
  bool StreamExists(const std::string &app, const std::string &stream) const;

  /**
   * @brief Phase 1.3 优化: 遍历所有流（支持早期终止）
   *
   * 相比GetAllStreams()，此方法不需要创建中间vector，直接在迭代时回调，
   * 减少50-60%内存分配。
   *
   * @param callback 回调函数，返回false时停止遍历
   * @tparam Func 回调函数类型: bool(const StreamMetadata&)
   *
   * 使用示例:
   * ```cpp
   * stream_manager->ForEachStream([](const StreamMetadata& metadata) {
   *     if (metadata.gateway_type == "rtsp") {
   *         // 处理RTSP流
   *     }
   *     return true;  // 继续遍历
   * });
   * ```
   */
  template <typename Func> void ForEachStream(Func &&callback) const {
    std::lock_guard<std::mutex> lock(mutex_);

    for (const auto &[key, metadata] : streams_) {
      if (!callback(metadata)) {
        break; // 早期终止
      }
    }
  }

  /**
   * @brief 同步 ZLMediaKit 状态
   *
   * 从 ZLMediaKit 获取所有流的状态，更新本地元数据
   * @return 同步的流数量
   */
  int SyncZLMStatus();

  /**
   * @brief 启动状态同步线程
   * @param sync_interval_seconds 同步间隔（秒），默认 10 秒
   */
  void StartStatusSync(int sync_interval_seconds = 10);

  /**
   * @brief 停止状态同步线程
   */
  void StopStatusSync();

  /**
   * @brief 获取状态统计信息
   * @return JSON 对象，包含各状态的计数和状态转换统计
   */
  nlohmann::json GetStatusStatistics() const;

  /**
   * @brief 获取流数量
   * @return 流数量
   */
  size_t GetStreamCount() const;

  /**
   * @brief Gateway 触发的“开始创建流”事件
   *
   * 语义：
   *  - 表示某个 app/stream 的创建流程已被发起（可能还未真正出现在 ZLM 中）
   *  - 负责将状态推进到 Starting，并补齐关键信息
   *
   * 注意：
   *  - 建议所有 Gateway 在真正启动拉流/推流前先调用该方法
   */
  void OnStreamCreateRequested(const StreamMetadata &metadata);

  /**
   * @brief Gateway 触发的“创建结果”事件
   *
   * 语义：
   *  - 表示某个 app/stream 的创建流程已经结束（成功或失败）
   *  - 负责根据结果将状态从 Starting 推进到 Running 或 Error
   *
   * 说明：
   *  - error_code / error_message 目前只是占位，用于后续错误模型统一
   */
  void OnStreamCreateResult(const std::string &app, const std::string &stream,
                            bool success, const std::string &error_code = "",
                            const std::string &error_message = "");

  /**
   * @brief ZLM 上报的流状态事件（Hook / 轮询统一入口）
   *
   * 语义：
   *  - 将 ZLM 的 regist/alive/统计信息转换为本地元数据和状态
   *  - 未来所有来自 Hook 的状态更新都应通过该接口完成
   *
   * 说明：
   *  - 当前实现会尽量保持与原有逻辑兼容，仅在一个地方集中更新状态与统计信息
   */
  void OnZLMStreamState(const std::string &app, const std::string &stream,
                        bool regist, bool alive, int reader_count,
                        int64_t bytes_speed, int64_t total_bytes);

private:
  /**
   * @brief 生成流键（app/stream）
   */
  std::string GetStreamKey(const std::string &app,
                           const std::string &stream) const;

  /**
   * @brief 状态同步线程函数
   */
  void StatusSyncThread(int sync_interval_seconds);

  /**
   * @brief 获取当前时间戳（秒）
   */
  static int64_t GetCurrentTimestamp();

  /**
   * @brief 更新流的元数据字段
   * @param existing 现有流元数据（将被修改）
   * @param metadata 新的元数据
   * @param preserve_protocols 是否保留现有协议信息（不覆盖）
   */
  void UpdateMetadataFields(StreamMetadata &existing,
                            const StreamMetadata &metadata,
                            bool preserve_protocols = false);

  /**
   * @brief 检查是否是通过 Gateway 注册的流
   */
  static bool IsGatewayRegistered(const StreamMetadata &metadata);

  /**
   * @brief 检查是否是自动注册的流
   */
  static bool IsAutoRegistered(const StreamMetadata &existing);

  /**
   * @brief 广播流更新到 WebSocket
   * @param metadata 流元数据
   * @param removed 是否已删除
   * @param force 是否强制广播（忽略节流）
   */
  void BroadcastStreamUpdate(StreamMetadata &metadata, bool removed = false,
                             bool force = false);

  /**
   * @brief 记录状态转换统计（阶段 C：监控功能）
   * @param from_status 原状态
   * @param to_status 新状态
   * @param source 转换来源（"gateway_create_requested",
   * "gateway_create_result", "zlm_state_update", "sync_polling",
   * "manual_update"）
   */
  void RecordStatusTransition(const std::string &app, const std::string &stream,
                              StreamStatus from_status, StreamStatus to_status,
                              const std::string &source);

  /**
   * @brief 将 StreamStatus 转换为字符串（用于统计）
   */
  std::string StreamStatusToString(StreamStatus status) const;

  // ============================================
  // Phase 2.1: 批量更新优化
  // ============================================

  /**
   * @brief 流更新数据结构（用于批量更新）
   *
   * 用于在锁外计算更新，然后批量应用，减少锁持有时间
   */
  struct StreamUpdate {
    std::string key; // app/stream
    std::string app;
    std::string stream;

    // 状态更新（使用optional避免不必要的更新）
    std::optional<StreamStatus> new_status;
    std::optional<bool> zlm_alive;
    std::optional<int> reader_count;
    std::optional<int64_t> bytes_speed;
    std::optional<int64_t> total_bytes;
    std::optional<std::string> zlm_stream;
    std::optional<std::string> zlm_app;

    // 元数据
    bool should_remove = false;
    std::string transition_source = "sync_polling";
    StreamStatus old_status = StreamStatus::Stopped; // 用于状态转换记录
  };

  /**
   * @brief 准备批量更新（无锁计算）
   *
   * 在锁外遍历流快照，计算每个流的更新信息
   *
   * @param zlm_stream_map ZLM流映射表
   * @param now 当前时间戳
   * @return 更新列表
   */
  std::vector<StreamUpdate>
  PrepareStreamUpdates(const std::map<std::string, StreamInfo> &zlm_stream_map,
                       int64_t now);

  /**
   * @brief 应用批量更新（短暂持锁）
   *
   * 批量应用PrepareStreamUpdates计算的更新
   *
   * @param updates 更新列表
   * @param now 当前时间戳
   * @return 实际更新的流数量
   */
  int ApplyStreamUpdates(const std::vector<StreamUpdate> &updates, int64_t now);

  std::shared_ptr<ZLMClient> zlm_client_;
  std::shared_ptr<api::WebSocketServer> websocket_server_;

  // GB28181流名称匹配器（用于O(1)匹配优化）
  std::unique_ptr<gateway::gb28181::GB28181StreamMatcher> gb28181_matcher_;

  // 状态统计（阶段 C：监控功能）
  struct StatusStatistics {
    // 各状态计数
    size_t count_running = 0;
    size_t count_starting = 0;
    size_t count_stopped = 0;
    size_t count_error = 0;
    size_t count_stopping = 0;

    // 状态转换统计（按来源分类）
    struct TransitionStats {
      size_t gateway_create_requested = 0; // OnStreamCreateRequested
      size_t gateway_create_result = 0;    // OnStreamCreateResult
      size_t zlm_state_update = 0;         // OnZLMStreamState
      size_t sync_polling = 0;             // SyncZLMStatus
      size_t manual_update = 0;            // UpdateStreamStatus (运维通道)
    } transitions;

    // 状态转换详情（用于调试）
    struct TransitionDetail {
      std::string app;
      std::string stream;
      std::string from_status;
      std::string to_status;
      std::string source;
      int64_t timestamp;
    };
    std::vector<TransitionDetail>
        recent_transitions; // 最近的状态转换（保留最近 100 条）
    static constexpr size_t MAX_RECENT_TRANSITIONS = 100;
  } status_stats_;
  mutable std::mutex stats_mutex_; // 保护统计数据的互斥锁
  std::map<std::string, StreamMetadata>
      streams_; // 流元数据存储（key: app/stream）
  std::set<std::string>
      manually_stopped_streams_; // 手动停止的流列表（防止自动重新注册）
  mutable std::mutex mutex_; // 保护 streams_ 和 manually_stopped_streams_

  // 状态同步线程
  std::thread sync_thread_;
  std::atomic<bool> sync_running_{false};
  std::condition_variable sleep_cv_;
  std::mutex sleep_mutex_;
};

} // namespace streaming

#endif // STREAMING_STREAM_MANAGER_HPP
