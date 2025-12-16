# 完整数据流文档

## 概述

本文档完整描述了从输入协议到输出协议的完整数据流，包括前端选择、后端Gateway处理、ZLMediaKit转换、以及前端播放的完整流程。

## 核心概念

- **流入协议（输入协议）**：流管理添加流时选择的协议，决定后端使用哪个Gateway处理
- **流出协议（输出协议）**：流管理添加流时同时指定的协议，决定Gateway是否需要转码以及前端使用哪个播放器
- **两者关联**：输入协议决定Gateway选择，输出协议决定Gateway转码策略和前端播放器选择

## 完整数据流

### 1️⃣ 前端流管理添加流（同时选择输入协议和输出协议）

**位置**：`frontend/src/components/streams/StreamForm.tsx`

**流程**：
- 用户在前端流管理页面**同时选择**：
  - **输入协议**（protocol）：rtsp/rtmp/hls/http-flv/dash/quic/onvif/isapi/dahua/psia
  - **输出协议**（output_protocol）：http-flv/hls/webrtc
- 填写 `source_url`、`app` 和 `stream` 名称
- 前端调用 `POST /api/v1/streams/start`

**请求示例**：
```json
{
  "protocol": "rtsp",
  "output_protocol": "webrtc",
  "source_url": "rtsp://example.com/stream",
  "app": "live",
  "stream": "test"
}
```

**输出协议说明**：
- `http-flv`：低延迟，适合大多数场景（默认值）
- `hls`：兼容性好，适合需要回放的场景
- `webrtc`：超低延迟，适合实时交互场景（系统会自动转码为 H.264 + AAC，使用低延迟编码参数以确保兼容性）

**代码位置**：
- `frontend/src/components/streams/StreamForm.tsx:71-92` - 表单字段（输入协议和输出协议）
- `frontend/src/components/streams/StreamForm.tsx:39-47` - 表单提交
- `frontend/src/api/streams.ts:24-26` - API调用

---

### 2️⃣ 后端接收输出协议并选择Gateway

**位置**：`src/api/http_server.cpp:468-610`

**流程**：
- `HandleStartStream` 接收请求，提取 `protocol` 和 `output_protocol` 参数
- 将 `output_protocol` 保存到 `metadata.output_protocol`
- 根据 `protocol` 参数选择对应的Gateway
- 设置 `metadata.gateway_type`
- 调用 Gateway 的 `Start()` 方法，**传递 `output_protocol` 参数**

**协议到Gateway映射**：
```cpp
// 原生协议（直接调用ZLM API，不使用FFmpeg）
protocol='rtmp'     -> native (直接调用 zlm_client->AddRTMPStream())
protocol='rtsp'     -> rtsp_gateway (根据 output_protocol 决定是否需要转码)
                       - 如果 output_protocol='webrtc'，强制转码为 H.264 + AAC，推流格式为 RTMP/FLV
                       - 如果 output_protocol='http-flv'/'hls' 且音频是aac，尝试直接代理；否则转码为 RTSP
protocol='http-flv' -> httpflv_gateway (Gateway内部调用 zlm_client->AddStreamProxy())
                       - 如果音频是AAC，直接代理（原生协议）
                       - 如果音频不是AAC，使用FFmpeg转码为AAC
protocol='hls'      -> hls_gateway (Gateway内部调用 zlm_client->AddStreamProxy()，原生协议)

// 非原生协议（使用FFmpeg转换）
protocol='dash'  -> dash_gateway (启动FFmpeg进程转换，根据 output_protocol 决定转码参数)
protocol='quic'  -> quic_gateway (启动FFmpeg进程转换，根据 output_protocol 决定转码参数)

// 设备发现协议
protocol='onvif' -> onvif_gateway (发现设备 -> 获取RTSP地址 -> 推送到ZLM)
protocol='isapi' -> isapi_gateway (发现设备 -> 获取RTSP地址 -> 推送到ZLM)
protocol='dahua' -> dahua_gateway (发现设备 -> 获取RTSP地址 -> 推送到ZLM)
protocol='psia'  -> psia_gateway (发现设备 -> 获取RTSP地址 -> 推送到ZLM)
```

**特殊逻辑**：
- **RTSP + WebRTC**：当 `protocol='rtsp'` 且 `output_protocol='webrtc'` 时，使用 `rtsp_gateway`，强制转码为 H.264 + AAC，推流格式为 RTMP/FLV
- **RTSP + HTTP-FLV/HLS**：当 `protocol='rtsp'` 且 `output_protocol='http-flv'` 或 `'hls'` 时，如果音频是AAC则直接调用 `AddRTSPStream()`（原生协议），否则转码
- **HTTP-FLV**：如果音频是AAC，直接调用 `AddStreamProxy()`（原生协议），否则使用FFmpeg转码为AAC
- **HLS**：直接调用 `AddStreamProxy()`（原生协议，无需转码）

**代码位置**：
- `src/api/http_server.cpp:476` - 接收 `output_protocol` 参数（默认值：`"http-flv"`）
- `src/api/http_server.cpp:493` - 保存到 `metadata.output_protocol`
- `src/api/http_server.cpp:501-572` - Gateway选择逻辑
- `src/api/http_server.cpp:572` - 调用 `gateway->Start(..., output_protocol)`

---

### 3️⃣ Gateway根据输出协议处理并推送到ZLMediaKit

**位置**：各Gateway实现（如 `src/gateway/rtsp/rtsp_gateway.cpp`）

**流程**：
- Gateway 的 `Start()` 方法接收 `output_protocol` 参数
- 根据 `output_protocol` 决定是否需要转码以及转码参数
- 将流推送到ZLMediaKit，标识为 `app/stream`
- 例如：`live/test`

**RTSP Gateway 处理逻辑**（`src/gateway/rtsp/rtsp_gateway.cpp:74-412`）：
- **接收 `output_protocol` 参数**，保存到流信息中
- **探测源流音频 codec**（使用 FFprobe）
- **根据 `output_protocol` 决定是否需要 FFmpeg 转码**：
  - `output_protocol='webrtc'`：
    - **强制使用 FFmpeg 转码**（`use_ffmpeg = true`）
    - 原因：即使音频编码已经是 opus 或 pcm_alaw，视频编码参数（GOP、帧率等）可能仍不符合 WebRTC 要求
    - 转码参数：H.264 + AAC，GOP=15，zerolatency，1080p，30fps，低延迟优化（bframes=0, ref=1, no-mbtree, rc-lookahead=0）
    - **推流格式**：RTMP/FLV（`-f flv`），推送到 ZLM 的 RTMP 端口
    - **原因**：与本地摄像头 WebRTC 链路保持一致，避免 RTSP→WebRTC 路径的兼容性问题
  - `output_protocol='http-flv'` 或 `'hls'`：
    - 如果源音频是 `aac`，尝试直接调用 `zlm_client->AddRTSPStream()`（直接代理）
    - 如果直接代理失败或流未活跃，回退到 FFmpeg 转码为 H.264 + AAC
    - 转码时推流格式：RTSP（`-f rtsp`），推送到 ZLM 的 RTSP 端口
  - 其他情况：启动 FFmpeg 转码

**DASH/QUIC Gateway 处理逻辑**（`src/gateway/dash/dash_gateway.cpp:52-462`）：
- **接收 `output_protocol` 参数**，保存到流信息中
- **根据 `output_protocol` 构建 FFmpeg 转码命令**：
  - `output_protocol='webrtc'`：转码为 H.264 + AAC（与 RTSP Gateway WebRTC 模式保持一致）
  - 其他情况：尝试复制流（`-c:v copy -c:a copy`）

**HTTP-FLV Gateway 处理逻辑**（`src/gateway/httpflv/httpflv_gateway.cpp:204-302`）：
- **接收 `output_protocol` 参数**（虽然接收，但因为是原生协议，不使用）
- **探测源流音频 codec**（使用 FFprobe）
- **根据音频 codec 决定是否需要 FFmpeg 转码**：
  - 如果音频是 `aac`，直接调用 `zlm_client->AddStreamProxy()`（原生协议，不转码）
  - 如果音频不是 `aac`，启动 FFmpeg 转码为 H.264 + AAC
- **性能优化**：优先使用原生协议，避免不必要的转码开销

**HLS Gateway 处理逻辑**（`src/gateway/hls/hls_gateway.cpp:23-59`）：
- **接收 `output_protocol` 参数**（虽然接收，但因为是原生协议，不使用）
- 直接调用 `zlm_client->AddStreamProxy()`，不使用 FFmpeg（原生协议）

**原生协议处理方式**：
- **RTMP**：在 `http_server.cpp` 中直接调用 `zlm_client->AddRTMPStream()`，不经过Gateway
- **RTSP**：通过 `rtsp_gateway` 处理，根据 `output_protocol` 和音频 codec 决定是否需要转码
- **HTTP-FLV**：通过 `httpflv_gateway` 处理，如果音频是AAC则直接调用 `AddStreamProxy()`（原生协议），否则转码
- **HLS**：通过 `hls_gateway` 处理，直接调用 `AddStreamProxy()`（原生协议，无需转码）

**代码位置**：
- `src/gateway/rtsp/rtsp_gateway.cpp:74-412` - RTSP Gateway Start 方法（根据 output_protocol 决定转码）
- `src/gateway/rtsp/rtsp_gateway.cpp:372-420` - BuildFFmpegCommand（根据 output_protocol 构建转码命令）
- `src/gateway/dash/dash_gateway.cpp:52-462` - DASH Gateway Start 方法
- `src/gateway/dash/dash_gateway.cpp:440-462` - BuildFFmpegCommand（根据 output_protocol 构建转码命令）
- `src/gateway/httpflv/httpflv_gateway.cpp:23-59` - HTTP-FLV Gateway（原生协议）
- `src/gateway/hls/hls_gateway.cpp:23-59` - HLS Gateway（原生协议）

---

### 4️⃣ ZLMediaKit自动提供多种输出协议

**关键特性**：
- 一旦流进入ZLMediaKit，**自动提供多种输出协议**
- 不需要额外配置，所有输出协议同时可用
- 输出协议与输入协议无关

**可用的输出协议URL**：
```
HTTP-FLV: http://zlm:8081/{app}/{stream}.live.flv
HLS:      http://zlm:8081/{app}/{stream}/hls.m3u8
WebRTC:   http://zlm:8081/index/api/webrtc?app={app}&stream={stream}&type=play
RTMP:     rtmp://zlm:1935/{app}/{stream}
RTSP:     rtsp://zlm:554/{app}/{stream}
```

**示例**（流标识：`live/test`）：
```
HTTP-FLV: http://localhost:8081/live/test.live.flv
HLS:      http://localhost:8081/live/test/hls.m3u8
WebRTC:   http://localhost:8081/index/api/webrtc?app=live&stream=test&type=play
```

**说明**：
- ZLMediaKit内部维护统一的流数据
- 根据请求的URL格式自动转换为对应的输出协议
- 这是ZLMediaKit的核心功能：协议互转

---

### 5️⃣ 前端视频展示使用流的输出协议

**位置**：`frontend/src/components/video/VideoGrid.tsx`

**流程**：
- **每个流使用自己的 `output_protocol`**（创建流时指定的）
- `VideoGrid` 从流数据中读取 `stream.output_protocol`
- 将 `output_protocol` 作为 `defaultProtocol` 传递给 `VideoPlayer`
- **不再有全局的协议下拉框**，每个流独立使用自己的输出协议

**代码位置**：
- `frontend/src/components/video/VideoGrid.tsx:65` - 全屏模式使用 `stream.output_protocol`
- `frontend/src/components/video/VideoGrid.tsx:128` - 网格模式使用 `stream.output_protocol`

---

### 6️⃣ 前端根据流的输出协议生成播放URL

**位置**：`frontend/src/utils/streamUrl.ts`

**流程**：
- `VideoPlayer` 接收 `defaultProtocol` 参数（来自 `stream.output_protocol`）
- 调用 `getStreamPlayURL(app, stream, defaultProtocol)`
- 根据输出协议生成对应的URL

**输出协议到URL映射**：
```typescript
protocol='http-flv' -> `${ZLM_BASE_URL}/${app}/${stream}.live.flv`
protocol='hls'      -> `${ZLM_BASE_URL}/${app}/${stream}/hls.m3u8`
protocol='webrtc'   -> `${ZLM_BASE_URL}/index/api/webrtc?app=${app}&stream=${stream}&type=play`
```

**代码位置**：
- `frontend/src/components/video/VideoPlayer.tsx:24-31` - 使用 defaultProtocol 生成 URL
- `frontend/src/utils/streamUrl.ts:10-25` - URL生成函数

---

### 7️⃣ 前端播放器播放

**位置**：`frontend/src/components/video/FLVPlayer.tsx`, `HLSPlayer.tsx`, `WebRTCPlayer.tsx`

**流程**：
- `VideoPlayer` 根据输出协议选择对应的播放器组件
- 播放器组件加载对应的URL并播放

**输出协议到播放器映射**：
```typescript
protocol='http-flv' -> <FLVPlayer url={url} />
protocol='hls'      -> <HLSPlayer url={url} />
protocol='webrtc'   -> <WebRTCPlayer url={url} />
```

**代码位置**：
- `frontend/src/components/video/VideoPlayer.tsx:55-64` - 播放器选择
- `frontend/src/components/video/FLVPlayer.tsx` - FLV播放器
- `frontend/src/components/video/HLSPlayer.tsx` - HLS播放器
- `frontend/src/components/video/WebRTCPlayer.tsx` - WebRTC播放器

---

## 完整数据流示例

### 示例：RTSP输入 -> WebRTC输出

1. **前端流管理**：用户选择输入协议 `rtsp`，输出协议 `webrtc`，填写 `source_url=rtsp://example.com/stream`
2. **后端接收参数**：`http_server.cpp` 接收 `protocol='rtsp'` 和 `output_protocol='webrtc'`
3. **Gateway选择**：检测到 `output_protocol='webrtc'`，使用 `rtsp_gateway`（需要转码）
4. **RTSP Gateway处理**：
   - 探测源流音频 codec（仅用于日志记录）
   - **强制启动 FFmpeg 转码**为 H.264 + AAC（GOP=15，zerolatency，1080p，30fps）
   - 推送到ZLMediaKit（RTMP/FLV格式），标识为 `live/test`
5. **ZLMediaKit提供输出**：自动提供多种输出协议URL（包括WebRTC）
6. **前端使用输出协议**：`VideoGrid` 读取 `stream.output_protocol='webrtc'`
7. **生成播放URL**：`getStreamPlayURL('live', 'test', 'webrtc')` -> `http://localhost:8081/index/api/webrtc?app=live&stream=test&type=play`
8. **播放器播放**：`WebRTCPlayer` 加载并播放该URL

### 示例：RTSP输入 -> HTTP-FLV输出（无需转码）

1. **前端流管理**：用户选择输入协议 `rtsp`，输出协议 `http-flv`，填写 `source_url=rtsp://example.com/stream`
2. **后端接收参数**：`http_server.cpp` 接收 `protocol='rtsp'` 和 `output_protocol='http-flv'`
3. **Gateway选择**：检测到 `output_protocol='http-flv'`，直接调用 `zlm_client->AddRTSPStream()`（原生协议，不经过Gateway）
4. **推送到ZLMediaKit**：流被推送到ZLMediaKit，标识为 `live/test`
5. **ZLMediaKit提供输出**：自动提供多种输出协议URL
6. **前端使用输出协议**：`VideoGrid` 读取 `stream.output_protocol='http-flv'`
7. **生成播放URL**：`getStreamPlayURL('live', 'test', 'http-flv')` -> `http://localhost:8081/live/test.live.flv`
8. **播放器播放**：`FLVPlayer` 加载并播放该URL

### 示例：DASH输入 -> WebRTC输出

1. **前端流管理**：用户选择输入协议 `dash`，输出协议 `webrtc`，填写 `source_url=http://example.com/manifest.mpd`
2. **后端接收参数**：`http_server.cpp` 接收 `protocol='dash'` 和 `output_protocol='webrtc'`
3. **Gateway选择**：使用 `dash_gateway`
4. **DASH Gateway处理**：
   - 根据 `output_protocol='webrtc'` 构建 FFmpeg 转码命令（H.264 + AAC，低延迟参数）
   - 启动 FFmpeg 进程转换并推送到ZLMediaKit，标识为 `live/test`
5. **ZLMediaKit提供输出**：自动提供多种输出协议URL
6. **前端使用输出协议**：`VideoGrid` 读取 `stream.output_protocol='webrtc'`
7. **生成播放URL**：`getStreamPlayURL('live', 'test', 'webrtc')` -> `http://localhost:8081/index/api/webrtc?app=live&stream=test&type=play`
8. **播放器播放**：`WebRTCPlayer` 加载并播放该URL

---

## 关键点总结

1. **创建流时同时指定输入协议和输出协议**
   - 输入协议（protocol）决定后端使用哪个Gateway处理
   - 输出协议（output_protocol）决定Gateway是否需要转码以及前端使用哪个播放器
   - 两者在创建流时同时指定，影响整个数据流处理

2. **输出协议影响Gateway转码策略**
   - **RTSP Gateway**：根据 `output_protocol` 决定转码策略
     - WebRTC：**强制转码**为 H.264 + AAC（GOP=15，zerolatency，1080p，30fps），推流格式为 RTMP/FLV
     - HTTP-FLV/HLS：如果源音频是 AAC，尝试直接代理；否则转码为 H.264 + AAC，推流格式为 RTSP
   - **HTTP-FLV Gateway**：根据源流音频 codec 智能决定是否需要转码
     - 如果音频是 AAC，直接调用 `AddStreamProxy()`（原生协议，不转码）
     - 如果音频不是 AAC，使用 FFmpeg 转码为 H.264 + AAC
   - **HLS Gateway**：原生协议，直接调用 `AddStreamProxy()`，不使用 `output_protocol`（但接收参数）
   - **DASH/QUIC Gateway**：根据 `output_protocol` 决定转码参数
     - WebRTC：强制转码为 H.264 + AAC（与 RTSP Gateway WebRTC 模式保持一致）
     - 其他：尝试复制流（`-c:v copy -c:a copy`）

3. **ZLMediaKit自动提供多种输出协议**
   - 一旦流进入ZLMediaKit，所有输出协议同时可用
   - 不需要额外配置或转换
   - 这是ZLMediaKit的核心功能：协议互转

4. **前端使用流的输出协议播放**
   - 每个流使用自己的 `output_protocol`（创建流时指定的）
   - `VideoGrid` 从流数据中读取 `stream.output_protocol`
   - `VideoPlayer` 使用 `defaultProtocol` 参数（来自 `stream.output_protocol`）
   - **不再有全局的协议下拉框**，每个流独立使用自己的输出协议

5. **Gateway的作用**
   - 将各种输入协议转换为ZLMediaKit可接受的格式
   - **原生协议处理**：
     - RTMP：直接调用 `AddRTMPStream()`（不经过Gateway）
     - RTSP：通过 `rtsp_gateway`，根据 `output_protocol` 和音频 codec 智能决定是否需要转码
     - HTTP-FLV：通过 `httpflv_gateway`，如果音频是AAC则直接代理，否则转码
     - HLS：通过 `hls_gateway`，直接调用 `AddStreamProxy()`（原生协议）
   - **非原生协议**（dash/quic）：使用FFmpeg转换
   - **设备发现协议**（onvif/isapi/dahua/psia）：发现设备 -> 获取RTSP地址 -> 推送到ZLM
   - 根据 `output_protocol` 和源流编码智能决定转码策略，避免不必要的转码开销

---

## 代码文件索引

### 前端
- `frontend/src/components/streams/StreamForm.tsx` - 流管理表单（同时选择输入协议和输出协议）
- `frontend/src/components/streams/StreamTable.tsx` - 流列表表格（显示输出协议）
- `frontend/src/components/video/VideoGrid.tsx` - 视频网格（使用流的 output_protocol）
- `frontend/src/components/video/VideoPlayer.tsx` - 统一播放器组件（接收 defaultProtocol）
- `frontend/src/utils/streamUrl.ts` - URL生成工具
- `frontend/src/types/stream.ts` - 流类型定义（包含 output_protocol 字段）

### 后端
- `src/api/http_server.cpp:468-610` - 接收 output_protocol 参数并选择Gateway
- `src/api/http_server.cpp:398` - API返回流数据时包含 output_protocol
- `src/gateway/rtsp/rtsp_gateway.cpp` - RTSP Gateway（根据 output_protocol 决定转码）
- `src/gateway/dash/dash_gateway.cpp` - DASH Gateway（根据 output_protocol 决定转码参数）
- `src/gateway/httpflv/httpflv_gateway.cpp` - HTTP-FLV Gateway（原生协议）
- `src/gateway/hls/hls_gateway.cpp` - HLS Gateway（原生协议）
- `src/streaming/stream_manager.hpp:34` - StreamMetadata 包含 output_protocol 字段
- `src/streaming/zlmediakit/zlm_client.cpp` - ZLMediaKit客户端

