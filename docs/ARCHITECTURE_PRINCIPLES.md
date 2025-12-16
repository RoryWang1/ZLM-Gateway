# Gateway 架构设计宗旨

## 核心逻辑（必须记住）

### 1. 统一流管理 - Gateway 维护所有流的元数据

**核心原则**：
- ✅ **所有协议（原生和非原生）都通过 Gateway API 统一管理**
- ✅ **Gateway 作为统一入口，维护流的元数据**（app, stream, protocol, source_url, device_info 等）
- ✅ **原生协议**：Gateway API -> ZLMediaKit API -> ZLMediaKit 直接管理流（无 FFmpeg 拷贝，性能无损）
  - **重要**：原生协议直接推流到 ZLMediaKit，Gateway 只通过 API 来管理，不参与数据传输
  - Gateway 只负责：启动流（调用 ZLMediaKit API）、停止流（调用 ZLMediaKit API）、查询状态（查询 ZLMediaKit API）
- ✅ **非原生协议**：Gateway API -> Gateway 管理 -> FFmpeg 进程（需要协议转换）

**性能影响**：
- API 调用开销：微秒级（< 1ms），可忽略
- 流传输：原生协议无额外拷贝，性能无损
- 状态查询：需要查询 ZLMediaKit API，但这是必要的

**好处**：
- 统一接口：客户端只需调用 Gateway API，无需区分协议类型
- 统一日志：所有流的操作都有统一的日志记录
- 统一监控：可以在 Gateway 层面做统一的监控和统计
- 统一权限：可以在 Gateway 层面做权限验证和访问控制
- 统一错误处理：可以在 Gateway 层面做统一的错误处理和重试
- 统一配置：可以在 Gateway 层面做统一的配置管理

---

## 2. Gateway 的双重作用

### 作用 A：协议转换 Gateway

**功能**：将不支持的协议转换为支持的协议

**示例**：
- **RTSP Gateway**：
  - WebRTC 模式：RTSP -> FFmpeg -> RTMP/FLV -> ZLMediaKit（强制转码，确保编码参数符合 WebRTC 要求）
  - HTTP-FLV/HLS 模式：RTSP -> ZLMediaKit（如果源音频是 AAC，尝试直接代理；否则 FFmpeg 转码为 RTSP）
- **HTTP-FLV Gateway**：HTTP-FLV -> ZLMediaKit（优先直接代理，音频不是AAC时才使用FFmpeg转码）
- **DASH Gateway**：MPEG-DASH -> RTSP/RTMP -> ZLMediaKit（通过 FFmpeg）
- **HLS Gateway**：HLS -> ZLMediaKit（直接代理，原生协议）
- **QUIC Gateway**：QUIC+FEC-TS -> RTSP/RTMP -> ZLMediaKit（通过 FFmpeg）

**工作流程**：
```
源流（非原生协议）-> FFmpeg 拉流 -> 协议转换 -> 推送到 ZLMediaKit（RTSP/RTMP）
或
源流（原生协议，如HTTP-FLV/HLS）-> 直接代理到 ZLMediaKit（优先方案，性能最优）
或
源流（RTSP + WebRTC）-> FFmpeg 转码 -> RTMP/FLV -> ZLMediaKit（确保 WebRTC 兼容性）
```

---

### 作用 B：设备发现和适配 Gateway

**功能**：设备发现协议，发现设备并获取流地址

**示例**：
- **ONVIF Gateway**：
  - 使用 WS-Discovery 协议发现 ONVIF 设备
  - 调用 ONVIF Media 服务获取设备的 RTSP 流地址
  - 管理设备的流（可以直接推流，因为 RTSP 是原生协议）
  
- **ISAPI Gateway**：
  - 发现海康 ISAPI 设备
  - 调用 ISAPI API 获取设备的 RTSP 流地址
  - 管理设备的流

- **大华 Gateway**：
  - 发现大华设备
  - 调用大华 API 获取设备的 RTSP 流地址
  - 管理设备的流

- **PSIA Gateway**：
  - 发现 PSIA 设备
  - 调用 PSIA API 获取设备的 RTSP 流地址
  - 管理设备的流

**工作流程**：
```
1. 设备发现
   ONVIF Gateway -> WS-Discovery 扫描网络 -> 发现 ONVIF 设备

2. 获取流地址
   ONVIF Gateway -> 调用 ONVIF Media API -> 获取 RTSP 流地址

3. 流管理
   ONVIF Gateway -> 启动流 -> 推送到 ZLMediaKit
   （可以直接推流，因为 RTSP 是原生协议，无需 FFmpeg）

4. 统一管理
   Gateway -> 维护设备信息和流元数据
```

---

## 3. 架构优势

✅ **统一管理**：所有流（无论来源）都通过 Gateway 统一管理
✅ **设备发现**：支持多种设备发现协议（ONVIF、ISAPI、大华、PSIA）
✅ **协议转换**：支持多种协议转换（HTTP-FLV、DASH、HLS、QUIC）
✅ **解耦设计**：Gateway 和 ZLMediaKit 解耦，易于替换
✅ **易于扩展**：可以轻松添加新的 Gateway（设备发现或协议转换）
✅ **性能优化**：
  - 原生协议（RTMP、RTSP、HTTP-FLV、HLS）优先直接推流，避免不必要的转码
  - 智能转码：根据 `output_protocol` 和源流编码智能决定是否需要转码
  - HTTP-FLV/HLS Gateway 优先使用直接代理（如果音频是AAC），性能最优

---

## 4. 实现要点

### 4.1 统一流管理实现

**Gateway 需要维护的流元数据**：
```cpp
struct StreamMetadata {
    std::string app;
    std::string stream;
    std::string protocol;        // "rtsp", "rtmp", "http-flv", "onvif", etc.
    std::string output_protocol; // "http-flv", "hls", "webrtc"（输出协议）
    std::string source_url;      // 源流地址
    std::string device_id;       // 设备 ID（如果是设备发现协议）
    std::string device_type;     // 设备类型（"onvif", "isapi", "dahua", etc.）
    std::string gateway_type;    // Gateway类型（"native", "ffmpeg", "device"）
    GatewayStatus status;        // 流状态（0=stopped, 1=starting, 2=running, 3=stopping, 4=error）
    int64_t create_time;         // 创建时间（Unix时间戳，秒）
    int64_t last_update_time;    // 最后更新时间（Unix时间戳，秒）
    int pid;                     // 进程ID（如果是FFmpeg进程）
    // ... 其他元数据
};
```

**统一状态查询**：
- 原生协议：Gateway 查询 ZLMediaKit API 获取实际状态（`zlm_alive` 字段）
- 非原生协议：Gateway 查询自己的状态（FFmpeg 进程状态）和 ZLMediaKit 状态
- 设备发现协议：Gateway 查询设备状态和流状态
- 状态码：0=stopped, 1=starting, 2=running, 3=stopping, 4=error

### 4.2 设备发现 Gateway 实现

**ONVIF Gateway 示例**：
```cpp
class ONVIFGateway : public GatewayBase {
    // 1. 设备发现
    std::vector<ONVIFDevice> DiscoverDevices();
    
    // 2. 获取流地址
    std::vector<std::string> GetDeviceRTSPURLs(const std::string& device_id);
    
    // 3. 启动流（直接推流到 ZLMediaKit，因为 RTSP 是原生协议）
    bool Start(const std::string& source_url, 
               const std::string& target_app,
               const std::string& target_stream) override;
    
    // 4. 统一管理
    // 维护设备信息和流元数据
};
```

---

## 5. 总结

**Gateway 的核心价值**：
1. **统一流管理**：所有流都通过 Gateway 统一管理，维护完整的元数据（包括 `output_protocol`）
2. **协议转换**：将不支持的协议转换为支持的协议
3. **设备发现**：支持多种设备发现协议，自动发现设备并获取流地址
4. **性能优化**：
   - 原生协议（RTMP、RTSP、HTTP-FLV、HLS）优先直接推流
   - 智能转码：根据 `output_protocol` 和源流编码智能决定是否需要转码
   - HTTP-FLV/HLS Gateway 优先使用直接代理（如果音频是AAC），避免不必要的转码开销
5. **输出协议支持**：支持为每个流指定输出协议（http-flv/hls/webrtc），前端根据输出协议选择播放器
6. **WebRTC 优化**：RTSP + WebRTC 模式强制使用 FFmpeg 转码，推流格式为 RTMP/FLV，与本地摄像头 WebRTC 链路保持一致，确保浏览器解码兼容性

**这个架构设计是项目的核心宗旨，必须严格遵守！**

