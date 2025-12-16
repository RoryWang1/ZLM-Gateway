# 流生命周期完整逻辑链

本文档详细描述添加流和删除流的完整逻辑链，包括前端事件、API传输、后端处理、FFmpeg判断、ZLMediaKit交互等每个步骤。

---

## 一、添加流完整流程

### 1.1 前端用户操作

**位置**: `frontend/src/components/streams/StreamForm.tsx`

**用户操作**:
1. 用户打开"启动流"对话框
2. 填写表单字段：
   - **输入协议** (protocol): 从下拉框选择 `rtsp` / `rtmp` / `http-flv` / `hls` / `dash` / `quic`
   - **输出协议** (output_protocol): 从下拉框选择 `http-flv` / `hls` / `webrtc`
   - **源地址** (source_url): 输入流源地址，例如 `rtsp://example.com/stream`
   - **应用名** (app): 输入应用名，默认 `live`
   - **流名** (stream): 输入流名，例如 `test_stream`
3. 点击"启动"按钮

**表单验证**:
- 所有字段必填
- 源地址格式验证（前端基础验证）

**代码位置**: `StreamForm.tsx:39-47` (handleOk函数)

---

### 1.2 前端API调用

**位置**: `frontend/src/api/streams.ts`

**API调用**:
```typescript
startStream({
  protocol: "rtsp",
  output_protocol: "webrtc",
  source_url: "rtsp://example.com/stream",
  app: "live",
  stream: "test_stream"
})
```

**HTTP请求**:
- **方法**: `POST`
- **URL**: `/api/v1/streams/start`
- **Content-Type**: `application/json`
- **请求体**:
```json
{
  "protocol": "rtsp",
  "output_protocol": "webrtc",
  "source_url": "rtsp://example.com/stream",
  "app": "live",
  "stream": "test_stream"
}
```

**字段说明**:
- `protocol` (string, 必需): 输入协议类型
- `output_protocol` (string, 必需): 输出协议类型，默认 `http-flv`
- `source_url` (string, 必需): 源流地址
- `app` (string, 必需): 应用名，默认 `live`
- `stream` (string, 必需): 流名

**代码位置**: `streams.ts:24-26`

---

### 1.3 后端HTTP服务器接收请求

**位置**: `src/api/http_server.cpp:483-625`

**处理函数**: `HandleStartStream`

**步骤1: 解析请求体**
- 解析JSON请求体
- 提取字段：
  - `app` (默认值: `"live"`)
  - `stream` (必需)
  - `source_url` (必需)
  - `protocol` (默认值: `"rtsp"`)
  - `output_protocol` (默认值: `"http-flv"`)

**步骤2: 参数验证**
- 检查 `stream` 是否为空
- 检查 `source_url` 是否为空
- 如果任一为空，返回400错误：
```json
{
  "code": -1,
  "msg": "stream and source_url cannot be empty"
}
```

**步骤3: 创建流元数据对象**
- 创建 `StreamMetadata` 对象
- 设置字段：
  - `app`: 从请求获取
  - `stream`: 从请求获取
  - `protocol`: 从请求获取
  - `output_protocol`: 从请求获取
  - `source_url`: 从请求获取
  - `status`: `StreamStatus::Starting` (状态码: 1)

**代码位置**: `http_server.cpp:487-510`

---

### 1.4 Gateway选择逻辑

**位置**: `src/api/http_server.cpp:512-599`

**逻辑判断流程**:

```
判断 protocol 值:
├─ 如果是 "rtmp"
│  └─ 使用原生方式: 直接调用 zlm_client->AddRTMPStream()
│     └─ gateway_type = "native"
│
└─ 如果是其他协议
   └─ 根据 protocol 选择对应的 Gateway:
      ├─ "rtsp" → rtsp_gateway (gateway_type="rtsp_gateway")
      │  └─ 注意：RTSP协议统一使用rtsp_gateway，无论output_protocol是什么
      ├─ "http-flv" → httpflv_gateway (gateway_type="httpflv_gateway")
      ├─ "hls" → hls_gateway (gateway_type="hls_gateway")
      ├─ "dash" → dash_gateway (gateway_type="dash_gateway")
      ├─ "quic" → quic_gateway (gateway_type="quic_gateway")
      ├─ "onvif" → onvif_gateway (gateway_type="onvif_gateway")
      ├─ "isapi" → isapi_gateway (gateway_type="isapi_gateway")
      ├─ "dahua" → dahua_gateway (gateway_type="dahua_gateway")
      └─ "psia" → psia_gateway (gateway_type="psia_gateway")
```

**错误处理**:
- 如果Gateway指针为空（未启用），返回500错误：
```json
{
  "code": -1,
  "msg": "Gateway for protocol {protocol} is not enabled"
}
```
- 如果协议不支持，返回400错误：
```json
{
  "code": 400,
  "msg": "Unsupported protocol"
}
```

**代码位置**: `http_server.cpp:516-584`

---

### 1.5 Gateway处理（以RTSP Gateway为例）

**位置**: `src/gateway/rtsp/rtsp_gateway.cpp:150-412`

**步骤1: 检查流是否已存在**
- 生成流ID: `{app}/{stream}`
- 检查流是否已在Gateway中运行
- 如果已运行，直接返回成功
- 如果存在但未运行，先停止旧进程

**步骤2: 创建流信息对象**
- 创建 `StreamInfo` 对象
- 设置字段：
  - `source_url`: 源流地址
  - `target_app`: 目标应用名
  - `target_stream`: 目标流名
  - `output_protocol`: 输出协议
  - `status`: `GatewayStatus::Starting`

**步骤3: 探测源流音频编码**
- 使用 `ffprobe` 探测源流的音频编码格式
- 记录到 `info.source_audio_codec`

**步骤4: FFmpeg转码判断逻辑**

```
判断是否需要FFmpeg转码:

如果 output_protocol == "webrtc":
  └─ use_ffmpeg = true (强制转码)
     - 原因：即使音频编码已经是 opus 或 pcm_alaw，视频编码参数（GOP、帧率等）可能仍不符合 WebRTC 要求
     - 统一使用 FFmpeg 转码，应用 WebRTC 优化的编码参数（GOP=15, zerolatency, H.264+AAC）

如果 output_protocol == "http-flv" 或 "hls":
  ├─ 如果源音频是 "aac"
  │  └─ use_ffmpeg = false (尝试直接代理)
  └─ 否则
     └─ use_ffmpeg = true (需要转码为 AAC)

其他情况:
  └─ use_ffmpeg = true (强制转码)
```

**步骤5A: 直接代理模式（use_ffmpeg = false）**

1. 调用 `zlm_client->AddRTSPStream(app, stream, source_url)`:
   - 内部调用ZLMediaKit API: `/index/api/addStreamProxy`
   - 参数: secret, vhost, app, stream, url, schema, retry_count=3
   - 如果返回code=0，表示成功

2. 如果API调用失败:
   - 记录错误日志
   - 回退到FFmpeg转码模式（设置 `use_ffmpeg = true`）

3. 如果API调用成功，等待并检查流是否活跃:
   - **重试机制**: 最多等待10秒，每2秒检查一次（共5次检查）
   - **检查方法**: 调用 `zlm_client->GetStreamInfo(app, stream)` 获取流信息
   - **活跃判断**: 使用 `ZLMClient::IsStreamActive()` 判断:
     - `alive == true` (流在ZLMediaKit中存活)
     - `bytes_speed > 0` (有数据传输)
   - **如果流活跃**: 设置状态为 `Running`，记录PID=0（无FFmpeg进程），返回成功
   - **如果10秒内流未活跃**: 
     - 记录警告日志
     - 调用 `zlm_client->DeleteStream()` 清理已创建的流
     - 回退到FFmpeg转码模式（设置 `use_ffmpeg = true`）

**步骤5B: FFmpeg转码模式（use_ffmpeg = true）**

1. 构建FFmpeg命令：
   - 根据 `output_protocol` 选择转码参数
   - **WebRTC模式**: 
     - 视频: H.264, preset=veryfast, tune=zerolatency, GOP=15, 码率=2500k（默认，可智能分配）, 分辨率=1920x1080, 帧率=30fps
     - 音频: AAC, 码率=128k, 采样率=44100Hz, 立体声
     - 关键 x264 参数: `bframes=0:ref=1:no-mbtree:rc-lookahead=0:scenecut=0`（低延迟优化）
     - **推流格式**: RTMP/FLV（`-f flv`），推送到 `rtmp://localhost:1935/{app}/{stream}?secret={zlm_secret}`
     - **原因**: 与本地摄像头 WebRTC 链路保持一致，避免 RTSP→WebRTC 路径的兼容性问题
   - **HTTP-FLV/HLS模式**: 
     - 视频: H.264, preset=veryfast, GOP=25, 码率=3500k（默认，可智能分配）, 分辨率=1920x1080, 帧率=30fps
     - 音频: AAC, 码率=128k, 采样率=44100Hz, 立体声
     - **推流格式**: RTSP（`-f rtsp`），推送到 `rtsp://localhost:554/{app}/{stream}?secret={zlm_secret}`

2. 通过 `ProcessManager` 启动FFmpeg进程:
   - 如果ProcessManager可用，使用ProcessManager统一管理
   - 否则使用fork+exec直接启动进程
   - 记录进程PID到流信息中

3. 等待500ms后检查进程状态:
   - 如果进程启动失败（PID无效），设置状态为 `Error`，返回失败

4. 等待2秒后再次检查进程状态:
   - 如果进程已退出，设置状态为 `Error`，返回失败
   - 如果进程仍在运行，继续下一步

5. 等待3秒后检查ZLMediaKit中是否有流:
   - 调用 `zlm_client->GetStreamInfo()` 查询流状态
   - 如果流已建立且活跃（`alive=true` 且有数据传输），设置状态为 `Running`，返回成功

6. 如果3秒后流未建立，再等待5秒（总共8秒）:
   - 再次检查流状态
   - 如果流已建立，设置状态为 `Running`，返回成功
   - 如果仍未建立，检查进程是否还在运行:
     - 如果进程已退出，设置状态为 `Error`，返回失败
     - 如果进程仍在运行但流未建立，设置状态为 `Error`，返回失败（可能是源流不存在）

**代码位置**: `rtsp_gateway.cpp:150-412`

---

### 1.6 ZLMediaKit交互

**位置**: `src/streaming/zlmediakit/zlm_client.cpp`

**API调用1: AddRTSPStream (直接代理模式)**

**请求**:
- **URL**: `{zlm_api_url}/index/api/addStreamProxy`
- **方法**: `POST`
- **参数**:
```json
{
  "secret": "{zlm_secret}",
  "vhost": "__defaultVhost__",
  "app": "live",
  "stream": "test_stream",
  "url": "rtsp://example.com/stream",
  "schema": "",
  "retry_count": 3
}
```

**响应**:
```json
{
  "code": 0,
  "msg": "success"
}
```

**API调用2: FFmpeg推流到ZLMediaKit**

FFmpeg命令推流到ZLMediaKit的RTMP地址：
```
rtmp://localhost:1935/live/test_stream?secret={zlm_secret}
```

ZLMediaKit自动接收RTMP推流，创建流记录。

**代码位置**: `zlm_client.cpp:74-118`

---

### 1.7 StreamManager注册流

**位置**: `src/streaming/stream_manager.cpp:29-107`

**步骤1: 检查流是否已存在**
- 生成流键: `{app}/{stream}`
- 如果流已存在，更新元数据（保留原始创建时间）

**步骤2: 设置时间戳**
- `create_time`: 当前Unix时间戳（秒）
- `last_update_time`: 当前Unix时间戳（秒）

**步骤3: 保存流元数据**
- 将流元数据保存到 `streams_` map中

**步骤4: WebSocket广播**
- 如果WebSocket服务器已启用，广播流更新：
```json
{
  "app": "live",
  "stream": "test_stream",
  "status": 1
}
```

**代码位置**: `stream_manager.cpp:29-107`

---

### 1.8 后端HTTP响应

**位置**: `src/api/http_server.cpp:603-624`

**成功响应**:
```json
{
  "code": 0,
  "msg": "success"
}
```

**失败响应**:
```json
{
  "code": 500,
  "msg": "Failed to start stream"
}
```

或
```json
{
  "code": -1,
  "msg": "{error_message}"
}
```

**代码位置**: `http_server.cpp:603-624`

---

### 1.9 前端接收响应并更新UI

**位置**: `frontend/src/components/streams/StreamTable.tsx` 和相关组件

**成功处理**:
1. 关闭"启动流"对话框
2. 刷新流列表（调用 `getStreams()` API）
3. 显示成功提示消息

**失败处理**:
1. 显示错误提示消息
2. 保持对话框打开，允许用户修改后重试

**流列表更新**:
- 调用 `GET /api/v1/streams` 获取最新流列表
- 更新表格显示
- 新流显示状态为 `starting` (状态码: 1)

**代码位置**: 前端流管理相关组件

---

## 二、删除流完整流程

### 2.1 前端用户操作

**位置**: `frontend/src/components/streams/StreamTable.tsx`

**用户操作**:
1. 用户在流列表中点击"删除"按钮
2. 弹出确认对话框："确定要删除这个流吗？删除后无法恢复"
3. 用户点击"确定"

**代码位置**: `StreamTable.tsx:139-149`

---

### 2.2 前端API调用

**位置**: `frontend/src/api/streams.ts`

**API调用方式1: DELETE方法**
```typescript
deleteStream("live", "test_stream")
```

**HTTP请求**:
- **方法**: `DELETE`
- **URL**: `/api/v1/streams/live/test_stream`

**API调用方式2: POST方法**
```typescript
stopStream({
  app: "live",
  stream: "test_stream"
})
```

**HTTP请求**:
- **方法**: `POST`
- **URL**: `/api/v1/streams/stop`
- **Content-Type**: `application/json`
- **请求体**:
```json
{
  "app": "live",
  "stream": "test_stream"
}
```

**字段说明**:
- `app` (string, 必需): 应用名
- `stream` (string, 必需): 流名

**代码位置**: `streams.ts:31-40`

---

### 2.3 后端HTTP服务器接收请求

**位置**: `src/api/http_server.cpp:628-863`

**处理函数**: `HandleStopStream`

**步骤1: 解析请求参数**
- 支持两种方式：
  1. 路径参数（DELETE方法）: `/api/v1/streams/:app/:stream`
  2. JSON body（POST方法）: `POST /api/v1/streams/stop`

**步骤2: 参数验证**
- 检查 `app` 和 `stream` 是否都存在
- 如果缺失，返回400错误：
```json
{
  "code": -1,
  "msg": "Missing required parameters: app and stream"
}
```

**步骤3: 获取流元数据**
- 从 `StreamManager` 获取流元数据
- 判断流是否在StreamManager中：
  - `stream_in_manager = !(metadata.app.empty() && metadata.stream.empty())`
- 判断流是否已停止：
  - `stream_stopped = (metadata.status == StreamStatus::Stopped)`

**步骤4: 处理流不在StreamManager中的情况**
- 如果流不在StreamManager中，可能是直接推送到ZLMediaKit的源流
- 尝试直接从ZLMediaKit删除：
  - 调用 `zlm_client->DeleteStream(app, stream)`
- 如果删除成功，返回成功响应：
```json
{
  "code": 0,
  "msg": "Stream deleted from ZLMediaKit (not in StreamManager)"
}
```
- 如果删除失败，返回404错误：
```json
{
  "code": -1,
  "msg": "Stream not found in StreamManager or ZLMediaKit"
}
```

**步骤5: 处理流已停止的情况**
- 如果流状态已经是 `Stopped`，返回404错误：
```json
{
  "code": -1,
  "msg": "Stream already stopped"
}
```

**代码位置**: `http_server.cpp:628-713`

---

### 2.4 更新流状态为停止中

**位置**: `src/api/http_server.cpp:715-716`

**操作**:
- 调用 `stream_manager->UpdateStreamStatus(app, stream, StreamStatus::Stopping)`
- 状态码: 3 (stopping)

**代码位置**: `http_server.cpp:716`

---

### 2.5 Gateway停止逻辑

**位置**: `src/api/http_server.cpp:718-810`

**根据Gateway类型选择停止方式**:

```
判断 gateway_type:
├─ "httpflv_gateway" 
│  └─ httpflv_gateway->Stop() (内部会停止FFmpeg进程 + 调用ZLM删除)
│     └─ 额外调用 zlm_client->DeleteStream() (确保清理)
│
├─ "dash_gateway"
│  └─ dash_gateway->Stop() (内部会停止FFmpeg进程)
│     └─ 额外调用 zlm_client->DeleteStream() (确保清理)
│
├─ "hls_gateway"
│  └─ hls_gateway->Stop() (内部会停止FFmpeg进程)
│     └─ 额外调用 zlm_client->DeleteStream() (确保清理)
│
├─ "quic_gateway"
│  └─ quic_gateway->Stop() (内部会停止FFmpeg进程)
│     └─ 额外调用 zlm_client->DeleteStream() (确保清理)
│
├─ "rtsp_gateway"
│  └─ rtsp_gateway->Stop() (只调用ZLM删除，不处理FFmpeg)
│     └─ 如果使用FFmpeg，需要额外通过ProcessManager停止进程
│     └─ 额外调用 zlm_client->DeleteStream() (确保清理)
│
├─ "onvif_gateway" / "isapi_gateway" / "dahua_gateway" / "psia_gateway"
│  └─ {gateway}->Stop() (只调用ZLM删除)
│     └─ 额外调用 zlm_client->DeleteStream() (确保清理)
│
└─ "native"
   └─ 直接调用 zlm_client->DeleteStream()
```

**注意**: 所有Gateway的Stop方法执行后，HTTP服务器层都会额外调用一次 `zlm_client->DeleteStream()` 以确保ZLMediaKit中的流记录被清理。

**Gateway Stop操作** (不同Gateway的处理方式):

**RTSP Gateway** (`rtsp_gateway.cpp:480-504`):
- 对于直接代理模式（不使用FFmpeg）: 只调用 `zlm_client->DeleteStream()`，ZLMediaKit会自动停止拉流
- 对于FFmpeg转码模式: 同样只调用 `zlm_client->DeleteStream()`，但FFmpeg进程需要通过ProcessManager停止（在HTTP服务器层处理）
- 从Gateway的流列表中移除流记录

**HTTP-FLV Gateway** (`httpflv_gateway.cpp:307-333`):
1. 查找流ID: `{app}/{stream}`
2. 如果流存在且使用FFmpeg:
   - 调用 `StopFFmpegProcess()` 停止FFmpeg进程
   - 如果使用ProcessManager，通过ProcessManager停止
   - 否则发送 `SIGTERM` 信号，等待5秒，如果未退出则发送 `SIGKILL`
   - **注意**（2025-12-05 修复）：SIGKILL 后增加等待时间（最多 2 秒，每 100ms 检查一次），确保进程完全退出。如果 2 秒后进程仍存在，记录警告但认为停止成功（可能是 zombie 进程）
3. 调用 `zlm_client->DeleteStream()` 清理ZLMediaKit中的流记录
4. 从Gateway的流列表中移除
5. 返回成功/失败

**DASH/QUIC/HLS Gateway**:
- 处理方式类似HTTP-FLV Gateway
- 先停止FFmpeg进程，再清理ZLMediaKit流记录

**设备发现Gateway (ONVIF/ISAPI/Dahua/PSIA)**:
- 直接调用 `zlm_client->DeleteStream()`，因为设备发现后获取的是RTSP地址，推送到ZLMediaKit后由ZLMediaKit管理

**代码位置**: `http_server.cpp:722-810`, 各Gateway的Stop方法实现

---

### 2.6 ZLMediaKit删除流

**位置**: `src/streaming/zlmediakit/zlm_client.cpp:167-268`

**删除策略**:

**方式1: delStreamProxy (优先)**
- **URL**: `{zlm_api_url}/index/api/delStreamProxy`
- **方法**: `POST`
- **参数**:
```json
{
  "secret": "{zlm_secret}",
  "key": "__defaultVhost__/live/test_stream"
}
```

**响应**:
```json
{
  "code": 0,
  "msg": "success"
}
```

**方式2: close_streams (回退)**
- 如果 `delStreamProxy` 失败，尝试 `close_streams`
- **URL**: `{zlm_api_url}/index/api/close_streams`
- **方法**: `POST`
- **参数**:
```json
{
  "secret": "{zlm_secret}",
  "schema": "",
  "vhost": "__defaultVhost__",
  "app": "live",
  "stream": "test_stream",
  "force": 1
}
```

**代码位置**: `zlm_client.cpp:167-268`

---

### 2.7 StreamManager注销流

**位置**: `src/streaming/stream_manager.cpp:109-136`

**步骤1: 查找流**
- 生成流键: `{app}/{stream}`
- 如果流不存在，返回失败

**步骤2: 记录手动停止**
- 将流键添加到 `manually_stopped_streams_` 集合
- 防止状态同步线程自动重新注册该流

**步骤3: 删除流记录**
- 从 `streams_` map中删除流记录

**步骤4: WebSocket广播**
- 广播流删除事件：
```json
{
  "app": "live",
  "stream": "test_stream",
  "status": 0,
  "removed": true
}
```

**代码位置**: `stream_manager.cpp:109-136`

---

### 2.8 后端HTTP响应

**位置**: `src/api/http_server.cpp:824-863`

**成功响应**:
```json
{
  "code": 0,
  "msg": "Stream stopped successfully"
}
```

**失败响应**:
```json
{
  "code": -1,
  "msg": "Failed to stop stream: {error_message}"
}
```

**代码位置**: `http_server.cpp:824-863`

---

### 2.9 前端接收响应并更新UI

**位置**: 前端流管理相关组件

**成功处理**:
1. 刷新流列表（调用 `getStreams()` API）
2. 显示成功提示消息
3. 流从列表中消失

**失败处理**:
1. 显示错误提示消息
2. 流状态可能更新为 `error` 或保持原状态

**流列表更新**:
- 调用 `GET /api/v1/streams` 获取最新流列表
- 已删除的流不再出现在列表中

---

## 三、状态同步机制

### 3.1 自动状态同步

**位置**: `src/streaming/stream_manager.cpp:186-500`

**同步线程**:
- 每10秒执行一次（可配置）
- 从ZLMediaKit获取所有流的状态
- 更新StreamManager中的流状态

**同步逻辑**:
1. 获取ZLMediaKit中的所有流
2. 对于StreamManager中的每个流：
   - 检查在ZLMediaKit中是否存在
   - 如果存在且状态为Running，更新为Running
   - 如果不存在且状态为Running，更新为Stopped
3. 对于ZLMediaKit中存在但StreamManager中不存在的流：
   - 如果不在 `manually_stopped_streams_` 中，自动注册
   - 如果已手动停止，不自动注册

**代码位置**: `stream_manager.cpp:186-500`

---

## 四、WebSocket实时更新

### 4.1 流状态变更广播

**位置**: `src/api/websocket_server.cpp` 和 `src/streaming/stream_manager.cpp`

**广播时机**:
1. 流注册时 (`RegisterStream`)
2. 流状态更新时 (`UpdateStreamStatus`)
3. 流注销时 (`UnregisterStream`)

**广播消息格式**:
```json
{
  "app": "live",
  "stream": "test_stream",
  "status": 2,
  "removed": false
}
```

**前端接收**:
- 前端WebSocket客户端接收消息
- 更新本地流列表状态
- 无需手动刷新页面

---

## 五、错误处理总结

### 5.1 添加流常见错误

1. **参数缺失**: 返回400错误
2. **Gateway未启用**: 返回500错误
3. **协议不支持**: 返回400错误
4. **FFmpeg启动失败**: 返回500错误
5. **流未建立**: 返回500错误

### 5.2 删除流常见错误

1. **参数缺失**: 返回400错误
2. **流不存在**: 返回404错误
3. **流已停止**: 返回404错误
4. **Gateway停止失败**: 返回500错误（已修复，见下方说明）
5. **ZLMediaKit删除失败**: 记录警告，但可能仍返回成功

**修复说明**（2025-12-05）：
- **问题**：本地摄像头流删除时，前端收到 500 错误，但流实际上已被清理
- **修复**：
  1. 改进 `ProcessManager::StopFFmpegProcess`：SIGKILL 后增加等待时间（最多 2 秒），确保进程完全退出
  2. 改进 `LocalCameraGateway::Stop`：即使停止失败，也清理流记录（避免重复删除和错误响应）
- **结果**：删除流功能正常工作，前端不再收到 500 错误

---

## 六、关键字段说明

### 6.1 StreamMetadata字段

- `app` (string): 应用名
- `stream` (string): 流名
- `protocol` (string): 输入协议
- `output_protocol` (string): 输出协议
- `source_url` (string): 源流地址
- `device_id` (string): 设备ID（设备发现协议）
- `device_type` (string): 设备类型
- `status` (int): 流状态 (0=stopped, 1=starting, 2=running, 3=stopping, 4=error)
- `create_time` (int64): 创建时间（Unix时间戳，秒）
- `last_update_time` (int64): 最后更新时间（Unix时间戳，秒）
- `pid` (int): 进程ID（FFmpeg进程）
- `gateway_type` (string): Gateway类型
- `zlm_alive` (bool): ZLMediaKit中流是否存活
- `reader_count` (int): 当前观看者数量
- `bytes_speed` (int64): 字节速度（字节/秒）
- `total_bytes` (int64): 总字节数

### 6.2 API响应字段

**成功响应**:
```json
{
  "code": 0,
  "msg": "success"
}
```

**错误响应**:
```json
{
  "code": -1,
  "msg": "error message"
}
```

**流列表响应**:
```json
{
  "code": 0,
  "data": [
    {
      "app": "live",
      "stream": "test_stream",
      "protocol": "rtsp",
      "output_protocol": "webrtc",
      "gateway_type": "rtsp_gateway",
      "status": 2,
      "status_text": "running",
      "source_url": "rtsp://example.com/stream",
      "create_time": 1703123456,
      "last_update_time": 1703123500,
      "pid": 12345,
      "cpu_usage": 5.2,
      "memory_usage": 52428800,
      "zlm_alive": true,
      "reader_count": 1,
      "bytes_speed": 1024000,
      "total_bytes": 1073741824
    }
  ]
}
```

---

## 七、流程图

### 7.1 添加流流程图

```
[用户填写表单]
    ↓
[前端验证] → [验证失败] → [显示错误]
    ↓ [验证成功]
[POST /api/v1/streams/start]
    ↓
[后端解析请求] → [参数缺失] → [返回400错误]
    ↓ [参数完整]
[创建StreamMetadata] (status=Starting)
    ↓
[选择Gateway]
    ↓
[Gateway.Start()]
    ↓
[探测音频编码]
    ↓
[判断是否需要FFmpeg]
    ├─ [不需要] → [直接代理] → [等待流活跃] → [成功/失败]
    └─ [需要] → [构建FFmpeg命令] → [启动进程] → [等待流建立] → [成功/失败]
    ↓
[StreamManager.RegisterStream()]
    ↓
[WebSocket广播]
    ↓
[返回HTTP响应]
    ↓
[前端更新UI]
```

### 7.2 删除流流程图

```
[用户点击删除]
    ↓
[确认对话框]
    ↓
[DELETE /api/v1/streams/:app/:stream]
    ↓
[后端解析参数] → [参数缺失] → [返回400错误]
    ↓ [参数完整]
[获取流元数据] → [流不存在] → [尝试ZLM删除] → [返回结果]
    ↓ [流存在]
[检查流状态] → [已停止] → [返回404错误]
    ↓ [未停止]
[更新状态为Stopping]
    ↓
[根据Gateway类型停止]
    ├─ [Gateway.Stop()] → [停止FFmpeg进程]
    └─ [zlm_client.DeleteStream()] → [删除ZLM流]
    ↓
[StreamManager.UnregisterStream()]
    ↓
[WebSocket广播]
    ↓
[返回HTTP响应]
    ↓
[前端更新UI]
```

---

## 八、时间线示例

### 8.1 添加流时间线

```
T+0ms:   用户点击"启动"按钮
T+10ms:  前端发送POST请求
T+50ms:  后端接收请求，开始处理
T+100ms: Gateway选择完成
T+200ms: 开始探测音频编码（ffprobe）
T+1500ms: 音频编码探测完成
T+1500ms: 判断是否需要FFmpeg
T+1510ms: 启动FFmpeg进程（如果需要）
T+2000ms: FFmpeg进程启动完成
T+5000ms: 检查ZLMediaKit中流是否建立
T+5000ms: 流已建立，状态更新为Running
T+5010ms: StreamManager注册流
T+5020ms: WebSocket广播
T+5030ms: 返回HTTP响应
T+5100ms: 前端接收响应，更新UI
```

### 8.2 删除流时间线

```
T+0ms:   用户点击"删除"按钮
T+100ms: 用户确认删除
T+150ms: 前端发送DELETE请求
T+200ms: 后端接收请求，开始处理
T+250ms: 获取流元数据
T+300ms: 更新状态为Stopping
T+350ms: Gateway.Stop() 开始执行
T+400ms: 发送SIGTERM给FFmpeg进程
T+1000ms: FFmpeg进程退出
T+1050ms: zlm_client.DeleteStream() 执行
T+1200ms: ZLMediaKit删除流成功
T+1250ms: StreamManager注销流
T+1260ms: WebSocket广播
T+1270ms: 返回HTTP响应
T+1350ms: 前端接收响应，更新UI
```

---

## 九、总结

本文档详细描述了添加流和删除流的完整逻辑链，包括：

1. **前端操作**: 用户交互、表单验证、API调用
2. **API传输**: 请求格式、响应格式、字段说明
3. **后端处理**: 参数解析、Gateway选择、状态管理
4. **Gateway处理**: FFmpeg判断、进程管理、流建立
5. **ZLMediaKit交互**: 流添加、流删除、状态查询
6. **状态同步**: 自动同步、WebSocket广播
7. **错误处理**: 各种错误情况的处理方式

每个步骤都包含了详细的逻辑判断、字段说明和代码位置，便于理解和维护。

