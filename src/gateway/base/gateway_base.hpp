#ifndef GATEWAY_BASE_GATEWAY_BASE_HPP
#define GATEWAY_BASE_GATEWAY_BASE_HPP

#include <string>
#include <memory>
#include <functional>
#include <map>
#include <mutex>
#include <thread>
#include <chrono>
#include <signal.h>
#include <unistd.h>
#include <errno.h>
#include "streaming/zlmediakit/zlm_client.hpp"
#include "streaming/stream_manager.hpp"
#include "process/process_manager.hpp"
#include "process/process_monitor.hpp"
#include "utils/logger.hpp"
#include "common/result.hpp"

namespace gateway {

/**
 * @brief Gateway 状态
 */
enum class GatewayStatus {
    Stopped,    // 已停止
    Starting,   // 启动中
    Running,    // 运行中
    Stopping,   // 停止中
    Error       // 错误
};

/**
 * @brief Gateway 基础类
 * 
 * 所有协议 Gateway 的基类，定义了统一的接口
 */
class GatewayBase {
public:
    virtual ~GatewayBase() = default;

    /**
     * @brief 启动 Gateway
     * @param source_url 源流 URL
     * @param target_app 目标应用名
     * @param target_stream 目标流名
     * @return Result<void>
     */
    virtual Result<void> Start(const std::string& source_url,
                      const std::string& target_app,
                      const std::string& target_stream,
                      const std::string& output_protocol = "") = 0;

    /**
     * @brief 停止 Gateway
     * @param target_app 目标应用名
     * @param target_stream 目标流名
     * @return Result<void>
     */
    virtual Result<void> Stop(const std::string& target_app,
                     const std::string& target_stream) = 0;

    /**
     * @brief 检查 Gateway 是否运行中
     * @param target_app 目标应用名
     * @param target_stream 目标流名
     * @return 是否运行中
     */
    virtual bool IsRunning(const std::string& target_app,
                          const std::string& target_stream) = 0;

    /**
     * @brief 获取 Gateway 状态
     * @param target_app 目标应用名
     * @param target_stream 目标流名
     * @return Gateway 状态
     */
    virtual GatewayStatus GetStatus(const std::string& target_app,
                                   const std::string& target_stream) = 0;

    /**
     * @brief 获取协议名称
     * @return 协议名称（如 "rtsp", "rtmp"）
     */
    virtual std::string GetProtocol() const = 0;

    /**
     * @brief 获取 Gateway 类型名称
     * @return Gateway 类型名称（如 "rtsp_gateway", "httpflv_gateway"）
     * 
     * 默认实现：protocol + "_gateway"
     * 子类可以重写此方法以提供自定义的类型名称
     */
    virtual std::string GetGatewayType() const {
        return GetProtocol() + "_gateway";
    }

protected:
    /**
     * @brief 生成流的唯一标识符
     * @param app 应用名
     * @param stream 流名
     * @return 唯一标识符
     */
    std::string GenerateStreamId(const std::string& app, const std::string& stream) const {
        return app + "/" + stream;
    }

    /**
     * @brief 简单的 IsRunning 实现辅助函数
     * @tparam StreamInfoType 流信息类型（需要有 status 字段）
     * @param stream_id 流ID
     * @param streams 流映射（非const，因为需要加锁）
     * @param mutex 互斥锁
     * @return 是否运行中
     */
    template<typename StreamInfoType>
    static bool IsRunningSimple(
        const std::string& stream_id,
        std::map<std::string, StreamInfoType>& streams,
        std::mutex& mutex) {
        std::lock_guard<std::mutex> lock(mutex);
        auto it = streams.find(stream_id);
        if (it == streams.end()) {
            return false;
        }
        return it->second.status == GatewayStatus::Running;
    }

    /**
     * @brief 简单的 GetStatus 实现辅助函数
     * @tparam StreamInfoType 流信息类型（需要有 status 字段）
     * @param stream_id 流ID
     * @param streams 流映射（非const，因为需要加锁）
     * @param mutex 互斥锁
     * @return Gateway 状态
     */
    template<typename StreamInfoType>
    static GatewayStatus GetStatusSimple(
        const std::string& stream_id,
        std::map<std::string, StreamInfoType>& streams,
        std::mutex& mutex) {
        std::lock_guard<std::mutex> lock(mutex);
        auto it = streams.find(stream_id);
        if (it == streams.end()) {
            return GatewayStatus::Stopped;
        }
        return it->second.status;
    }

    /**
     * @brief 原生协议的 IsRunning 实现辅助函数（检查 ZLM 中的流）
     * @tparam StreamInfoType 流信息类型（需要有 status 字段）
     * @param stream_id 流ID
     * @param target_app 目标应用名
     * @param target_stream 目标流名
     * @param streams 流映射（非const，因为需要更新状态）
     * @param mutex 互斥锁
     * @param zlm_client ZLM客户端
     * @return 是否运行中
     */
    template<typename StreamInfoType>
    static bool IsRunningNative(
        const std::string& stream_id,
        const std::string& target_app,
        const std::string& target_stream,
        std::map<std::string, StreamInfoType>& streams,
        std::mutex& mutex,
        std::shared_ptr<streaming::ZLMClient> zlm_client) {
        std::lock_guard<std::mutex> lock(mutex);
        auto it = streams.find(stream_id);
        if (it == streams.end()) {
            return false;
        }

        // 检查流是否在 ZLM 中存在
        auto stream_info = zlm_client->GetStreamInfo(target_app, target_stream);
        if (!stream_info.app.empty()) {
            it->second.status = GatewayStatus::Running;
            return true;
        } else {
            it->second.status = GatewayStatus::Stopped;
            return false;
        }
    }

    /**
     * @brief 原生协议的 GetStatus 实现辅助函数（检查 ZLM 中的流）
     * @tparam StreamInfoType 流信息类型（需要有 status 字段）
     * @param stream_id 流ID
     * @param target_app 目标应用名
     * @param target_stream 目标流名
     * @param streams 流映射（非const，因为需要更新状态）
     * @param mutex 互斥锁
     * @param zlm_client ZLM客户端
     * @return Gateway 状态
     */
    template<typename StreamInfoType>
    static GatewayStatus GetStatusNative(
        const std::string& stream_id,
        const std::string& target_app,
        const std::string& target_stream,
        std::map<std::string, StreamInfoType>& streams,
        std::mutex& mutex,
        std::shared_ptr<streaming::ZLMClient> zlm_client) {
        std::lock_guard<std::mutex> lock(mutex);
        auto it = streams.find(stream_id);
        if (it == streams.end()) {
            return GatewayStatus::Stopped;
        }

        // 检查流是否在 ZLM 中存在
        auto stream_info = zlm_client->GetStreamInfo(target_app, target_stream);
        if (!stream_info.app.empty()) {
            it->second.status = GatewayStatus::Running;
        } else {
            it->second.status = GatewayStatus::Stopped;
        }

        return it->second.status;
    }

    /**
     * @brief 原生协议的 Stop 实现辅助函数（调用 ZLM API 删除流）
     * @tparam StreamInfoType 流信息类型（需要有 target_app 和 target_stream 字段）
     * @param stream_id 流ID
     * @param target_app 目标应用名
     * @param target_stream 目标流名
     * @param streams 流映射（非const，因为需要删除）
     * @param mutex 互斥锁
     * @param zlm_client ZLM客户端
     * @param gateway_name Gateway名称（用于日志）
     * @return 是否成功
     */
    template<typename StreamInfoType>
    static Result<void> StopNative(
        const std::string& stream_id,
        const std::string& target_app,
        const std::string& target_stream,
        std::map<std::string, StreamInfoType>& streams,
        std::mutex& mutex,
        std::shared_ptr<streaming::ZLMClient> zlm_client,
        const std::string& gateway_name) {
        std::lock_guard<std::mutex> lock(mutex);
        
        auto it = streams.find(stream_id);
        if (it == streams.end()) {
            LOG_WARN("流不存在: {}/{}", target_app, target_stream);
            return Result<void>::Success();
        }

        // 调用 ZLM API 删除流（ZLM 会停止拉流）
        bool success = zlm_client->DeleteStream(target_app, target_stream);
        
        streams.erase(it);
        
        if (success) {
            LOG_INFO("{} Gateway 停止成功: {}/{}", gateway_name, target_app, target_stream);
            return Result<void>::Success();
        } else {
            LOG_WARN("{} Gateway 停止失败（流可能已不存在）: {}/{}", gateway_name, target_app, target_stream);
            return Result<void>::Failure(gateway::InternalServerException("Failed to stop native stream"));
        }
    }

    /**
     * @brief 析构函数中停止所有流的辅助函数（原生协议）
     * @tparam StreamInfoType 流信息类型（需要有 target_app 和 target_stream 字段）
     * @param streams 流映射（非const，因为需要删除）
     * @param mutex 互斥锁
     * @param zlm_client ZLM客户端
     */
    template<typename StreamInfoType>
    static void StopAllStreamsNative(
        std::map<std::string, StreamInfoType>& streams,
        std::mutex& mutex,
        std::shared_ptr<streaming::ZLMClient> zlm_client) {
        std::lock_guard<std::mutex> lock(mutex);
        for (auto& pair : streams) {
            zlm_client->DeleteStream(pair.second.target_app, pair.second.target_stream);
        }
        streams.clear();
    }

    /**
     * @brief 原生协议的 Start 实现辅助函数（直接调用 ZLM API）
     * @tparam StreamInfoType 流信息类型（需要有 source_url, target_app, target_stream, status 字段）
     * @param stream_id 流ID
     * @param source_url 源URL
     * @param target_app 目标应用名
     * @param target_stream 目标流名
     * @param streams 流映射（非const，因为需要添加）
     * @param mutex 互斥锁
     * @param zlm_client ZLM客户端
     * @param add_stream_callback 添加流的回调函数，返回是否成功
     * @param gateway_name Gateway名称（用于日志）
     * @return 是否成功
     */
    template<typename StreamInfoType>
    static Result<void> StartNative(
        const std::string& stream_id,
        const std::string& source_url,
        const std::string& target_app,
        const std::string& target_stream,
        std::map<std::string, StreamInfoType>& streams,
        std::mutex& mutex,
        std::shared_ptr<streaming::ZLMClient> /* zlm_client */,
        std::function<bool(const std::string&, const std::string&, const std::string&)> add_stream_callback,
        const std::string& gateway_name) {
        (void)gateway_name;  // 用于日志，但可能在某些情况下未使用
        std::lock_guard<std::mutex> lock(mutex);
        
        // 检查流是否已存在
        auto it = streams.find(stream_id);
        if (it != streams.end()) {
            if (it->second.status == GatewayStatus::Running) {
                LOG_WARN("流已运行: {}/{}", target_app, target_stream);
                return Result<void>::Success();
            }
        }

        // 调用 ZLM API 添加流
        if (!add_stream_callback(target_app, target_stream, source_url)) {
            LOG_ERROR("添加 {} 流到 ZLMediaKit 失败: {}/{}", gateway_name, target_app, target_stream);
            return Result<void>::Failure(gateway::InternalServerException("Failed to add stream to ZLMediaKit"));
        }

        // 创建流信息（只保存元数据，不启动 FFmpeg 进程）
        StreamInfoType info;
        info.source_url = source_url;
        info.target_app = target_app;
        info.target_stream = target_stream;
        info.status = GatewayStatus::Running;

        // 保存流信息
        streams[stream_id] = info;
        
        LOG_INFO("{} Gateway 启动成功（原生协议，直接到 ZLM）: {} -> {}/{}", gateway_name, source_url, target_app, target_stream);
        return Result<void>::Success();
    }

    /**
     * @brief 检查流是否已存在的辅助函数
     * @tparam StreamInfoType 流信息类型（需要有 status 字段）
     * @param stream_id 流ID
     * @param streams 流映射（非const，因为可能需要停止旧进程）
     * @param mutex 互斥锁
     * @param target_app 目标应用名（用于日志）
     * @param target_stream 目标流名（用于日志）
     * @param stop_old_process_callback 停止旧进程的回调函数（可选）
     * @return pair<bool, bool>：第一个bool表示流是否存在，第二个bool表示是否已运行（如果不存在则返回false）
     */
    template<typename StreamInfoType>
    static std::pair<bool, bool> CheckStreamExists(
        const std::string& stream_id,
        std::map<std::string, StreamInfoType>& streams,
        std::mutex& mutex,
        const std::string& target_app,
        const std::string& target_stream,
        std::function<void(StreamInfoType&)> stop_old_process_callback = nullptr) {
        std::lock_guard<std::mutex> lock(mutex);
        
        auto it = streams.find(stream_id);
        if (it != streams.end()) {
            if (it->second.status == GatewayStatus::Running) {
                LOG_WARN("流已运行: {}/{}", target_app, target_stream);
                return {true, true};
            }
            // 如果状态不是运行中，先停止旧进程
            if (stop_old_process_callback) {
                stop_old_process_callback(it->second);
            }
            return {true, false};
        }
        return {false, false};
    }

    /**
     * @brief 停止流的辅助函数（适用于使用 FFmpeg 的 Gateway）
     * @tparam StreamInfoType 流信息类型（需要有 target_app, target_stream 字段，以及 pid 字段）
     * @param stream_id 流ID
     * @param target_app 目标应用名
     * @param target_stream 目标流名
     * @param streams 流映射（非const，因为需要删除）
     * @param mutex 互斥锁
     * @param stop_ffmpeg_callback 停止 FFmpeg 进程的回调函数
     * @param delete_zlm_stream_callback 删除 ZLM 流的回调函数（可选）
     * @param gateway_name Gateway名称（用于日志）
     * @return 是否成功
     */
    template<typename StreamInfoType>
    static Result<void> StopWithFFmpeg(
        const std::string& stream_id,
        const std::string& target_app,
        const std::string& target_stream,
        std::map<std::string, StreamInfoType>& streams,
        std::mutex& mutex,
        std::function<bool(StreamInfoType&)> stop_ffmpeg_callback,
        std::function<void(const std::string&, const std::string&)> delete_zlm_stream_callback = nullptr,
        const std::string& gateway_name = "") {
        std::lock_guard<std::mutex> lock(mutex);
        
        auto it = streams.find(stream_id);
        if (it == streams.end()) {
            LOG_WARN("流不存在: {}/{}", target_app, target_stream);
            return Result<void>::Success();
        }

        // 停止 FFmpeg 进程
        bool success = stop_ffmpeg_callback(it->second);

        // 删除 ZLM 流（如果提供了回调）
        if (delete_zlm_stream_callback) {
            delete_zlm_stream_callback(target_app, target_stream);
        }
        
        streams.erase(it);
        
        if (success) {
            LOG_INFO("{} Gateway 停止成功: {}/{}", gateway_name, target_app, target_stream);
            return Result<void>::Success();
        } else {
            LOG_WARN("{} Gateway 停止失败（流可能已不存在）: {}/{}", gateway_name, target_app, target_stream);
            return Result<void>::Failure(gateway::InternalServerException("Failed to stop FFmpeg process"));
        }
    }

    /**
     * @brief 析构函数中停止所有流的辅助函数（适用于使用 FFmpeg 的 Gateway）
     * @tparam StreamInfoType 流信息类型（需要有 target_app, target_stream 字段）
     * @param streams 流映射（非const，因为需要删除）
     * @param mutex 互斥锁
     * @param stop_ffmpeg_callback 停止 FFmpeg 进程的回调函数（接收 StreamInfoType&）
     * @param delete_zlm_stream_callback 删除 ZLM 流的回调函数（可选，接收 target_app, target_stream）
     */
    template<typename StreamInfoType>
    static void StopAllStreamsWithFFmpeg(
        std::map<std::string, StreamInfoType>& streams,
        std::mutex& mutex,
        std::function<void(StreamInfoType&)> stop_ffmpeg_callback,
        std::function<void(const std::string&, const std::string&)> delete_zlm_stream_callback = nullptr) {
        std::lock_guard<std::mutex> lock(mutex);
        for (auto& pair : streams) {
            stop_ffmpeg_callback(pair.second);
            if (delete_zlm_stream_callback) {
                delete_zlm_stream_callback(pair.second.target_app, pair.second.target_stream);
            }
        }
        streams.clear();
    }

    /**
     * @brief 通用的基于 PID 的 FFmpeg 进程停止逻辑
     *
     * 适用于使用裸 PID + ProcessMonitor 管理 FFmpeg 的 Gateway（如 DASHGateway）。
     * 将统一的 SIGTERM/SIGKILL + 状态更新逻辑抽取出来，减少各 Gateway 内部重复代码。
     *
     * @tparam StreamInfoType 流信息类型（需要包含 pid 和 status 字段）
     * @param info 流信息（包含 pid 和 status）
     * @param gateway_name Gateway 名称（用于日志）
     * @return 是否成功停止进程
     */
    template<typename StreamInfoType>
    static Result<void> StopProcessByPID(StreamInfoType& info, const std::string& gateway_name) {
        if (info.pid <= 0) {
            info.status = GatewayStatus::Stopped;
            return Result<void>::Success();
        }

        info.status = GatewayStatus::Stopping;

        // 先检查进程是否还在运行
        if (!process::ProcessMonitor::IsProcessAlive(info.pid)) {
            // 进程已经不存在了
            info.pid = 0;
            info.status = GatewayStatus::Stopped;
            return Result<void>::Success();
        }

        // 发送 SIGTERM 信号
        if (::kill(info.pid, SIGTERM) != 0) {
            LOG_WARN("{} Gateway: 发送 SIGTERM 失败 (PID: {}): {}", gateway_name, info.pid, strerror(errno));
            // 如果进程不存在，认为停止成功
            if (errno == ESRCH) {
                info.pid = 0;
                info.status = GatewayStatus::Stopped;
                return Result<void>::Success();
            }
            return Result<void>::Failure(gateway::InternalServerException("Failed to send SIGTERM"));
        }

        // 等待进程退出（最多等待 5 秒）
        for (int i = 0; i < 50; ++i) {
            if (!process::ProcessMonitor::IsProcessAlive(info.pid)) {
                info.pid = 0;
                info.status = GatewayStatus::Stopped;
                return Result<void>::Success();
            }
            ::usleep(100000);  // 100ms
        }

        // 如果进程还在运行，发送 SIGKILL
        LOG_WARN("{} Gateway: 进程未响应 SIGTERM，发送 SIGKILL (PID: {})", gateway_name, info.pid);
        if (::kill(info.pid, SIGKILL) == 0) {
            ::usleep(100000);  // 等待 100ms
            if (!process::ProcessMonitor::IsProcessAlive(info.pid)) {
                info.pid = 0;
                info.status = GatewayStatus::Stopped;
                return Result<void>::Success();
            }
        } else {
            // SIGKILL 发送失败，检查进程是否已经退出
            if (errno == ESRCH || !process::ProcessMonitor::IsProcessAlive(info.pid)) {
                info.pid = 0;
                info.status = GatewayStatus::Stopped;
                return Result<void>::Success();
            }
        }

        // 最终检查：如果进程确实不存在了，认为停止成功
        if (!process::ProcessMonitor::IsProcessAlive(info.pid)) {
            info.pid = 0;
            info.status = GatewayStatus::Stopped;
            return Result<void>::Success();
        }

        info.status = GatewayStatus::Error;
        return Result<void>::Failure(gateway::InternalServerException("Failed to stop process"));
    }

    /**
     * @brief 等待进程启动并获取进程信息的辅助函数
     * @param process_manager 进程管理器
     * @param process_id 进程ID
     * @param initial_wait_ms 初始等待时间（毫秒），默认500ms
     * @param max_retries 最大重试次数，默认20次
     * @param retry_interval_ms 重试间隔（毫秒），默认100ms
     * @param pid 输出参数：进程PID
     * @param status 输出参数：进程状态（转换为GatewayStatus）
     * @return 是否成功获取到进程信息（pid > 0）
     */
    static bool WaitForProcessStart(
        std::shared_ptr<process::ProcessManager> process_manager,
        const std::string& process_id,
        int initial_wait_ms,
        int max_retries,
        int retry_interval_ms,
        pid_t& pid,
        GatewayStatus& status) {
        if (!process_manager) {
            return false;
        }

        // 初始等待
        if (initial_wait_ms > 0) {
            std::this_thread::sleep_for(std::chrono::milliseconds(initial_wait_ms));
        }

        // 循环等待进程启动
        for (int retry = 0; retry < max_retries; ++retry) {
            auto process_info = process_manager->GetProcessInfo(process_id);
            if (process_info.pid > 0) {
                pid = process_info.pid;
                status = (process_info.status == process::ProcessStatus::Running) ?
                        GatewayStatus::Running : GatewayStatus::Starting;
                return true;
            }
            if (retry < max_retries - 1) {
                std::this_thread::sleep_for(std::chrono::milliseconds(retry_interval_ms));
            }
        }

        // 最后一次尝试
        auto process_info = process_manager->GetProcessInfo(process_id);
        if (process_info.pid > 0) {
            pid = process_info.pid;
            status = (process_info.status == process::ProcessStatus::Running) ?
                    GatewayStatus::Running : GatewayStatus::Starting;
            return true;
        }

        return false;
    }

    /**
     * @brief 注册流到 StreamManager 的统一辅助函数
     * 
     * 所有 Gateway 在成功启动流后应调用此方法注册流到 StreamManager，
     * 以便前端能够获取到正确的流信息和播放协议。
     * 
     * @param stream_manager StreamManager 实例（如果为 nullptr 则不注册）
     * @param target_app 目标应用名
     * @param target_stream 目标流名
     * @param protocol 输入协议（如 "rtsp", "http-flv"）
     * @param output_protocol 输出协议（如 "webrtc", "http-flv", "hls"）
     * @param source_url 源流 URL
     * @param gateway_type Gateway 类型（如 "rtsp_gateway", "httpflv_gateway"）
     * @param gateway_status Gateway 状态（Running 或 Starting）
     * @param pid 进程 ID（如果使用 FFmpeg 转码，否则为 0）
     * @param device_id 设备 ID（可选，用于设备相关的流）
     * @param device_type 设备类型（可选，如 "local_camera"）
     */
    static void RegisterStreamToManager(
        std::shared_ptr<streaming::StreamManager> stream_manager,
        const std::string& target_app,
        const std::string& target_stream,
        const std::string& protocol,
        const std::string& output_protocol,
        const std::string& source_url,
        const std::string& gateway_type,
        GatewayStatus gateway_status,
        int pid = 0,
        const std::string& device_id = "",
        const std::string& device_type = "") {
        if (!stream_manager) {
            return;
        }
        
        streaming::StreamMetadata metadata;
        metadata.app = target_app;
        metadata.stream = target_stream;
        metadata.protocol = protocol;
        metadata.output_protocol = output_protocol;
        metadata.source_url = source_url;
        metadata.gateway_type = gateway_type;
        metadata.status = (gateway_status == GatewayStatus::Running) ? 
                        streaming::StreamStatus::Running : streaming::StreamStatus::Starting;
        metadata.pid = pid;
        if (!device_id.empty()) {
            metadata.device_id = device_id;
        }
        if (!device_type.empty()) {
            metadata.device_type = device_type;
        }
        
        stream_manager->RegisterStream(metadata);
        LOG_DEBUG("[{}] 流已注册到 StreamManager: {}/{} (output_protocol: {})", 
                gateway_type, target_app, target_stream, output_protocol);
    }
};

} // namespace gateway

#endif // GATEWAY_BASE_GATEWAY_BASE_HPP

