# 系统完整流程图

本文档使用 Mermaid 流程图展示系统的完整逻辑流程。

## 一、添加流完整流程图

```mermaid
flowchart TD
    A[用户填写表单] --> B{验证表单}
    B -->|验证失败| A
    B -->|验证成功| C[POST /api/v1/streams/start]
    
    C --> D[后端接收请求]
    D --> E[解析参数: protocol, output_protocol, source_url, app, stream]
    E --> F[创建 StreamMetadata]
    F --> G{判断 protocol}
    
    G -->|rtmp| H[直接调用 zlm_client->AddRTMPStream]
    G -->|rtsp| I[使用 rtsp_gateway]
    G -->|http-flv| J[使用 httpflv_gateway]
    G -->|hls| K[使用 hls_gateway]
    G -->|dash| L[使用 dash_gateway]
    G -->|quic| M[使用 quic_gateway]
    G -->|onvif/isapi/dahua/psia| N[使用对应设备发现 Gateway]
    
    I --> O[RTSP Gateway: 探测音频 codec]
    O --> P{判断 output_protocol}
    
    P -->|webrtc| Q[强制 FFmpeg 转码]
    P -->|http-flv/hls| R{源音频是 AAC?}
    
    R -->|是| S[尝试直接代理 AddRTSPStream]
    R -->|否| Q
    
    S --> T{流是否活跃?}
    T -->|是| U[注册到 StreamManager]
    T -->|否| Q
    
    Q --> V[构建 FFmpeg 命令]
    V --> W{output_protocol}
    
    W -->|webrtc| X[编码: H.264+AAC, GOP=15, zerolatency, 1080p, 30fps<br/>推流: RTMP/FLV]
    W -->|http-flv/hls| Y[编码: H.264+AAC, GOP=25, 1080p, 30fps<br/>推流: RTSP]
    
    X --> Z[启动 FFmpeg 进程]
    Y --> Z
    
    Z --> AA{进程启动成功?}
    AA -->|否| AB[返回错误]
    AA -->|是| AC[等待进程稳定]
    
    AC --> AD[检查 ZLM 中流是否建立]
    AD --> AE{流已建立?}
    AE -->|是| U
    AE -->|否| AF{进程还在运行?}
    AF -->|是| AD
    AF -->|否| AB
    
    U --> AG[WebSocket 广播]
    AG --> AH[返回 HTTP 响应]
    AH --> AI[前端更新 UI]
    
    H --> U
    J --> AJ[HTTP-FLV Gateway 处理]
    K --> AK[HLS Gateway 处理]
    L --> AL[DASH Gateway 处理]
    M --> AM[QUIC Gateway 处理]
    N --> AN[设备发现 Gateway 处理]
    
    AJ --> U
    AK --> U
    AL --> U
    AM --> U
    AN --> U
    
    style Q fill:#ff9999
    style X fill:#99ff99
    style Y fill:#99ff99
    style U fill:#9999ff
```

## 二、删除流完整流程图

```mermaid
flowchart TD
    A[用户点击删除] --> B[确认对话框]
    B --> C[DELETE /api/v1/streams/:app/:stream]
    
    C --> D[后端接收请求]
    D --> E[解析参数: app, stream]
    E --> F[从 StreamManager 获取流元数据]
    
    F --> G{流是否存在?}
    G -->|否| H[尝试直接从 ZLM 删除]
    G -->|是| I{流状态}
    
    I -->|已停止| J[返回 404 错误]
    I -->|未停止| K[更新状态为 Stopping]
    
    K --> L{判断 gateway_type}
    
    L -->|rtsp_gateway| M[rtsp_gateway->Stop]
    L -->|httpflv_gateway| N[httpflv_gateway->Stop]
    L -->|dash_gateway| O[dash_gateway->Stop]
    L -->|hls_gateway| P[hls_gateway->Stop]
    L -->|quic_gateway| Q[quic_gateway->Stop]
    L -->|native| R[直接调用 zlm_client->DeleteStream]
    
    M --> S{使用 FFmpeg?}
    S -->|是| T[通过 ProcessManager 停止 FFmpeg 进程]
    S -->|否| U[调用 zlm_client->DeleteStream]
    
    N --> T
    O --> T
    P --> T
    Q --> T
    
    T --> U
    U --> V[StreamManager.UnregisterStream]
    V --> W[WebSocket 广播]
    W --> X[返回 HTTP 响应]
    X --> Y[前端更新 UI]
    
    R --> V
    H --> Z{删除成功?}
    Z -->|是| X
    Z -->|否| AA[返回 404 错误]
    
    style K fill:#ff9999
    style T fill:#ffcc99
    style U fill:#99ff99
    style V fill:#9999ff
```

## 三、数据流完整流程图（输入协议 → 输出协议）

```mermaid
flowchart LR
    subgraph 输入层
        A1[RTSP 源]
        A2[RTMP 源]
        A3[HTTP-FLV 源]
        A4[HLS 源]
        A5[DASH 源]
        A6[本地摄像头]
        A7[设备发现协议]
    end
    
    subgraph Gateway 层
        B1[RTSP Gateway]
        B2[RTMP Gateway]
        B3[HTTP-FLV Gateway]
        B4[HLS Gateway]
        B5[DASH Gateway]
        B6[LocalCamera Gateway]
        B7[设备发现 Gateway]
    end
    
    subgraph 转码判断
        C1{需要转码?}
        C2[FFmpeg 转码]
        C3[直接代理]
    end
    
    subgraph ZLMediaKit
        D1[RTMP/FLV 流]
        D2[RTSP 流]
        D3[HLS 流]
    end
    
    subgraph 输出层
        E1[HTTP-FLV]
        E2[HLS]
        E3[WebRTC]
        E4[RTSP]
        E5[RTMP]
    end
    
    subgraph 前端播放
        F1[FLVPlayer]
        F2[HLSPlayer]
        F3[WebRTCPlayer]
    end
    
    A1 --> B1
    A2 --> B2
    A3 --> B3
    A4 --> B4
    A5 --> B5
    A6 --> B6
    A7 --> B7
    
    B1 --> C1
    B2 --> C3
    B3 --> C1
    B4 --> C3
    B5 --> C1
    B6 --> C2
    B7 --> B1
    
    C1 -->|WebRTC 模式| C2
    C1 -->|HTTP-FLV/HLS + AAC| C3
    C1 -->|其他| C2
    
    C2 -->|WebRTC| D1
    C2 -->|HTTP-FLV/HLS| D2
    C3 --> D2
    C3 --> D3
    
    D1 --> E1
    D1 --> E2
    D1 --> E3
    D2 --> E1
    D2 --> E2
    D2 --> E3
    D2 --> E4
    D3 --> E2
    
    E1 --> F1
    E2 --> F2
    E3 --> F3
    
    style C2 fill:#ff9999
    style C3 fill:#99ff99
    style D1 fill:#9999ff
    style E3 fill:#ffcc99
```

## 四、RTSP Gateway WebRTC 模式详细流程图

```mermaid
flowchart TD
    A[RTSP Gateway.Start] --> B[探测源流音频 codec]
    B --> C{output_protocol == 'webrtc'?}
    
    C -->|是| D[强制 use_ffmpeg = true]
    C -->|否| E{output_protocol == 'http-flv'/'hls'?}
    
    E -->|是| F{源音频是 AAC?}
    E -->|否| D
    
    F -->|是| G[尝试直接代理 AddRTSPStream]
    F -->|否| D
    
    G --> H{流是否活跃?}
    H -->|是| I[注册到 StreamManager, PID=0]
    H -->|否| D
    
    D --> J[构建 FFmpeg 命令]
    J --> K{output_protocol}
    
    K -->|webrtc| L[编码参数:<br/>- H.264 + AAC<br/>- GOP=15, zerolatency<br/>- 1080p, 30fps<br/>- bframes=0, ref=1<br/>- no-mbtree, rc-lookahead=0<br/><br/>推流格式: RTMP/FLV]
    
    K -->|http-flv/hls| M[编码参数:<br/>- H.264 + AAC<br/>- GOP=25<br/>- 1080p, 30fps<br/><br/>推流格式: RTSP]
    
    L --> N[启动 FFmpeg 进程]
    M --> N
    
    N --> O{进程启动成功?}
    O -->|否| P[返回错误]
    O -->|是| Q[等待进程稳定 500ms]
    
    Q --> R[等待 2 秒]
    R --> S{进程还在运行?}
    S -->|否| P
    S -->|是| T[等待 3 秒]
    
    T --> U[检查 ZLM 中流是否建立]
    U --> V{流已建立且活跃?}
    V -->|是| W[注册到 StreamManager, 记录 PID]
    V -->|否| X{再等待 5 秒}
    
    X --> Y{流已建立?}
    Y -->|是| W
    Y -->|否| Z{进程还在运行?}
    Z -->|否| P
    Z -->|是| P
    
    W --> AA[返回成功]
    I --> AA
    
    style D fill:#ff9999
    style L fill:#99ff99
    style M fill:#99ff99
    style W fill:#9999ff
    style I fill:#9999ff
```

## 五、前端播放流程图

```mermaid
flowchart TD
    A[前端获取流列表] --> B[VideoGrid 读取 stream.output_protocol]
    B --> C{output_protocol}
    
    C -->|http-flv| D[生成 HTTP-FLV URL<br/>http://zlm:8081/app/stream.live.flv]
    C -->|hls| E[生成 HLS URL<br/>http://zlm:8081/app/stream/hls.m3u8]
    C -->|webrtc| F[生成 WebRTC URL<br/>http://zlm:8081/index/api/webrtc?app=app&stream=stream&type=play]
    
    D --> G[FLVPlayer 组件]
    E --> H[HLSPlayer 组件]
    F --> I[WebRTCPlayer 组件]
    
    G --> J[使用 flv.js 播放]
    H --> K[使用 hls.js 播放]
    I --> L[使用 ZLMRTCClient 播放]
    
    J --> M[视频播放]
    K --> M
    L --> M
    
    style D fill:#99ff99
    style E fill:#99ff99
    style F fill:#ffcc99
    style M fill:#9999ff
```

## 六、关键决策点说明

### 1. FFmpeg 转码判断逻辑

```
RTSP Gateway:
├─ output_protocol == "webrtc"
│  └─ 强制转码 (use_ffmpeg = true)
│     └─ 原因：确保视频编码参数符合 WebRTC 要求
│
└─ output_protocol == "http-flv" || "hls"
   ├─ 源音频是 "aac"
   │  └─ 尝试直接代理 (use_ffmpeg = false)
   │     └─ 如果失败或流未活跃，回退到转码
   │
   └─ 源音频不是 "aac"
      └─ 强制转码 (use_ffmpeg = true)
```

### 2. 推流格式选择

```
RTSP Gateway FFmpeg 转码:
├─ output_protocol == "webrtc"
│  └─ 推流格式: RTMP/FLV (-f flv)
│     └─ 原因：与本地摄像头 WebRTC 链路保持一致
│
└─ output_protocol == "http-flv" || "hls"
   └─ 推流格式: RTSP (-f rtsp)
```

### 3. 编码参数选择

```
WebRTC 模式:
├─ 视频: H.264, preset=veryfast, tune=zerolatency
├─ GOP: 15 (小 GOP，降低延迟)
├─ 分辨率: 1920x1080
├─ 帧率: 30fps
├─ x264 参数: bframes=0:ref=1:no-mbtree:rc-lookahead=0:scenecut=0
└─ 音频: AAC, 128k, 44100Hz, 立体声

HTTP-FLV/HLS 模式:
├─ 视频: H.264, preset=veryfast
├─ GOP: 25
├─ 分辨率: 1920x1080
├─ 帧率: 30fps
└─ 音频: AAC, 128k, 44100Hz, 立体声
```

## 七、状态流转图

```mermaid
stateDiagram-v2
    [*] --> Stopped: 初始状态
    
    Stopped --> Starting: 用户添加流
    Starting --> Running: 流建立成功
    Starting --> Error: 流建立失败
    
    Running --> Stopping: 用户删除流
    Stopping --> Stopped: 删除成功
    Stopping --> Error: 删除失败
    
    Error --> Starting: 重试
    Error --> Stopped: 清理
    
    Running --> Error: 流异常中断
    
    note right of Starting
        等待 FFmpeg 进程启动
        等待 ZLM 流建立
    end note
    
    note right of Running
        流正常运行
        有数据传输
    end note
    
    note right of Stopping
        停止 FFmpeg 进程
        删除 ZLM 流
        清理资源
    end note
```

## 八、协议转换矩阵

| 输入协议 | 输出协议 | Gateway | 是否需要转码 | 推流格式 | 原因说明 |
|---------|---------|---------|------------|---------|---------|
| RTSP | WebRTC | RTSP Gateway | ✅ 强制 | RTMP/FLV | **必须转码**：1) 确保视频编码参数符合 WebRTC 低延迟要求（GOP=15, zerolatency, bframes=0 等）；2) 统一音频编码为 AAC；3) 推流格式使用 RTMP/FLV 而非 RTSP，与本地摄像头 WebRTC 链路保持一致，避免 RTSP→WebRTC 路径的兼容性问题 |
| RTSP | HTTP-FLV | RTSP Gateway | ⚠️ 条件 | RTSP | **条件转码**：如果源音频是 AAC 且视频参数合适，尝试直接代理（性能最优）；否则转码为 H.264+AAC，统一 GOP=25、分辨率、码率等参数，确保播放稳定性 |
| RTSP | HLS | RTSP Gateway | ⚠️ 条件 | RTSP | **条件转码**：如果源音频是 AAC 且视频参数合适，尝试直接代理；否则转码为 H.264+AAC，统一编码参数，确保 HLS 切片质量和播放兼容性 |
| RTMP | * | Native | ❌ 不需要 | RTMP | **无需转码**：RTMP 是 ZLMediaKit 原生协议，标准 H.264+AAC 流可直接推入，无需额外处理 |
| HTTP-FLV | * | HTTP-FLV Gateway | ⚠️ 条件 | RTSP | **条件转码**：如果源音频是 AAC，直接代理（性能最优）；否则转码为 AAC，确保音频兼容性 |
| HLS | * | HLS Gateway | ❌ 不需要 | HLS | **无需转码**：HLS 是 ZLMediaKit 原生协议，直接代理 HLS 文件/段，无需转码 |
| DASH | WebRTC | DASH Gateway | ✅ 需要 | RTSP/RTMP | **必须转码**：DASH 不是 ZLMediaKit 原生协议，需要 FFmpeg 拉取 DASH 流并转码为 WebRTC 兼容的 H.264+AAC 格式 |
| QUIC | WebRTC | QUIC Gateway | ✅ 需要 | RTSP/RTMP | **必须转码**：QUIC 是实验性协议，不是 ZLMediaKit 原生协议，需要 FFmpeg 转码为 WebRTC 兼容格式 |
| 本地摄像头 | WebRTC | LocalCamera Gateway | ✅ 需要 | RTMP/FLV | **必须转码**：摄像头原始输出需要编码为 H.264+AAC，应用 WebRTC 低延迟参数（GOP=15, zerolatency），确保浏览器解码兼容性 |
| 本地摄像头 | HTTP-FLV/HLS | LocalCamera Gateway | ✅ 需要 | RTMP/FLV | **必须转码**：摄像头原始输出需要编码为 H.264+AAC，应用标准编码参数（GOP=25），确保播放流畅度 |
| ONVIF | * | ONVIF Gateway → RTSP Gateway | ✅ 需要 | RTSP/RTMP | **必须转码**：ONVIF 是设备发现协议，最终获取 RTSP URL，按 RTSP Gateway 规则处理（WebRTC 强制转码，HTTP-FLV/HLS 条件转码） |
| ISAPI | * | ISAPI Gateway → RTSP Gateway | ✅ 需要 | RTSP/RTMP | **必须转码**：ISAPI 是设备发现协议，最终获取 RTSP URL，按 RTSP Gateway 规则处理 |
| Dahua | * | Dahua Gateway → RTSP Gateway | ✅ 需要 | RTSP/RTMP | **必须转码**：大华协议是设备发现协议，最终获取 RTSP URL，按 RTSP Gateway 规则处理 |
| PSIA | * | PSIA Gateway → RTSP Gateway | ✅ 需要 | RTSP/RTMP | **必须转码**：PSIA 是设备发现协议，最终获取 RTSP URL，按 RTSP Gateway 规则处理 |

---

**说明**：
- ✅ 强制：必须使用 FFmpeg 转码
- ⚠️ 条件：根据源流编码决定是否需要转码
- ❌ 不需要：直接代理或推流，无需转码

**转码原因总结**：
1. **协议不兼容**：输入协议不是 ZLMediaKit 原生协议（如 DASH、QUIC），需要转换为原生协议
2. **编码参数不匹配**：源流编码参数不符合输出协议要求（如 WebRTC 需要低延迟参数）
3. **音频编码不兼容**：源音频不是目标协议要求的编码格式（如 WebRTC/HTTP-FLV 需要 AAC）
4. **统一编码标准**：确保所有流使用统一的编码参数（GOP、分辨率、码率等），提升播放稳定性
5. **链路兼容性**：某些协议组合（如 RTSP→WebRTC）存在兼容性问题，改用更稳定的链路（RTSP→RTMP→WebRTC）

