# 本地摄像头数据链路详解

## 📊 完整数据流路径

```
本地摄像头设备
    ↓
FFmpeg 输入（avfoundation/v4l2）
    ↓
FFmpeg 编码（根据 output_protocol）
    ↓
RTMP/FLV 推流到 ZLM
    ↓
ZLMediaKit 接收并处理
    ↓
前端播放（根据 output_protocol）
```

## 🔍 详细链路分析

### 1. 输入层：本地摄像头设备

#### macOS (avfoundation)
```cpp
// 输入格式：-f avfoundation
// 输入设备：摄像头索引:音频设备索引
// 示例：-f avfoundation -framerate 30 -i "0:2"
```

**设备匹配逻辑**：
- 视频设备：摄像头索引（如 `0`, `1`, `2`）
- 音频设备：智能匹配
  - 优先使用自动匹配的音频设备索引（`audio_device_index`）
  - 回退到启发式匹配：
    - 如果摄像头名称包含 "1080P" 或 "USB Camera"，使用音频索引 `2`
    - 如果摄像头索引是 `0`，使用音频索引 `2`
    - 否则使用摄像头索引作为音频索引

#### Linux (v4l2)
```cpp
// 输入格式：-f v4l2
// 输入设备：/dev/video{index}
// 示例：-f v4l2 -framerate 30 -i /dev/video0
```

**注意**：Linux 下摄像头设备通常不包含音频，音频需要从 ALSA/PulseAudio 单独获取（当前实现未包含）

### 2. 编码层：FFmpeg 转码

根据 `output_protocol` 选择不同的编码参数：

#### WebRTC 模式

**视频编码**：
```bash
-c:v libx264
-preset ultrafast          # 或配置中的 preset
-tune zerolatency          # 低延迟调优
-g 10                      # GOP 大小（配置中的 gop_size）
-vf scale=1280x720         # 分辨率（配置中的 resolution）
-r 30                      # 帧率（配置中的 fps）
-threads 1                 # 线程数（配置中的 threads）
-x264-params keyint=10:min-keyint=10:scenecut=0:bframes=0:ref=1:no-mbtree:rc-lookahead=0
-b:v 2000k                 # 视频码率（智能分配或配置）
-maxrate 2000k
-bufsize 4000k
```

**音频编码**：
```bash
-c:a aac                   # AAC 编码（FLV 容器兼容）
-b:a 128k                  # 音频码率
-ar 48000                  # 采样率 48kHz（WebRTC 标准）
-ac 2                      # 立体声
```

**输出格式**：
```bash
-f flv                     # FLV 容器
rtmp://127.0.0.1:1935/live/{stream}?secret={secret}
```

#### HTTP-FLV / HLS 模式

**视频编码**：
```bash
-c:v libx264
-preset veryfast           # 或配置中的 preset
-tune zerolatency          # 低延迟调优
-g 25                      # GOP 大小（配置中的 gop_size）
-vf scale=1920x1080        # 分辨率（配置中的 resolution）
-r 30                      # 帧率（配置中的 fps）
-x264-params keyint=25:min-keyint=25:scenecut=0:bframes=0:ref=1:no-mbtree:rc-lookahead=0
-b:v 1800k                 # 视频码率（智能分配或配置）
-maxrate 1800k
-bufsize 3600k
```

**音频编码**：
```bash
-c:a aac                   # AAC 编码
-b:a 128k                  # 音频码率
-ar 44100                  # 采样率 44.1kHz（标准音频采样率）
-ac 2                      # 立体声
```

**输出格式**：
```bash
-f flv                     # FLV 容器
rtmp://127.0.0.1:1935/live/{stream}?secret={secret}
```

### 3. 推流层：RTMP/FLV 推送到 ZLM

**推流地址构建**：
```cpp
// 基础 URL
rtmp://127.0.0.1:{rtmp_port}/{target_app}/{target_stream}

// 如果配置了 secret，添加认证参数
?secret={secret}

// 完整示例
rtmp://127.0.0.1:1935/live/camera_2cd2adb83512f9d4?secret=UQyXemwV81qnNkuXQSp2eo5txJM35PZr
```

**推流协议**：
- **协议**：RTMP
- **容器**：FLV
- **视频编码**：H.264 (libx264)
- **音频编码**：AAC

### 4. ZLM 处理层：接收并转换

**ZLM 接收**：
```
RTMP 推流 → ZLM RTMP 端口 (1935)
    ↓
ZLM 解析 FLV 容器
    ↓
提取 H.264 视频 + AAC 音频
    ↓
存储为内部流格式
```

**ZLM 输出协议转换**：

#### WebRTC 输出
```
内部流（H.264 + AAC）
    ↓
ZLM 音频转码（AAC → Opus）⚠️ 需要 transcode 分支
    ↓
WebRTC SDP 生成
    ↓
前端 WebRTC 播放（Opus 音频）
```

**注意**：
- ZLM 主分支不支持 AAC → Opus 转码
- 需要使用 `transcode` 分支或专业版
- 如果使用主分支，音频轨道会被标记为 `inactive`

#### HTTP-FLV 输出
```
内部流（H.264 + AAC）
    ↓
ZLM 直接导出 FLV
    ↓
前端 FLV 播放（AAC 音频）
```

#### HLS 输出
```
内部流（H.264 + AAC）
    ↓
ZLM 生成 HLS 切片（.m3u8 + .ts）
    ↓
前端 HLS 播放（AAC 音频）
```

### 5. 前端播放层

#### WebRTC 播放
```
WebRTC URL: http://localhost:8081/index/api/webrtc?app=live&stream={stream}&type=play
    ↓
ZLMRTCClient.js 初始化
    ↓
WebRTC 信令交换（SDP）
    ↓
ICE 候选交换
    ↓
建立 WebRTC 连接
    ↓
接收 RTP 数据包（H.264 视频 + Opus 音频）
    ↓
浏览器解码并播放
```

#### HTTP-FLV 播放
```
FLV URL: http://localhost:8081/live/{stream}.live.flv
    ↓
FLVPlayer 组件（flv.js）
    ↓
HTTP 长连接接收 FLV 数据
    ↓
flv.js 解析 FLV 容器
    ↓
提取 H.264 视频 + AAC 音频
    ↓
浏览器解码并播放
```

#### HLS 播放
```
HLS URL: http://localhost:8081/live/{stream}/hls.m3u8
    ↓
HLSPlayer 组件（hls.js）
    ↓
HTTP 请求获取 .m3u8 播放列表
    ↓
按需请求 .ts 切片文件
    ↓
hls.js 解析 TS 容器
    ↓
提取 H.264 视频 + AAC 音频
    ↓
浏览器解码并播放
```

## 📋 完整 FFmpeg 命令示例

### WebRTC 模式

```bash
ffmpeg \
  -f avfoundation \
  -framerate 30 \
  -i "0:2" \
  -c:v libx264 \
  -preset ultrafast \
  -tune zerolatency \
  -g 10 \
  -vf scale=1280x720 \
  -r 30 \
  -threads 1 \
  -x264-params keyint=10:min-keyint=10:scenecut=0:bframes=0:ref=1:no-mbtree:rc-lookahead=0 \
  -b:v 2000k \
  -maxrate 2000k \
  -bufsize 4000k \
  -c:a aac \
  -b:a 128k \
  -ar 48000 \
  -ac 2 \
  -f flv \
  "rtmp://127.0.0.1:1935/live/camera_2cd2adb83512f9d4?secret=UQyXemwV81qnNkuXQSp2eo5txJM35PZr"
```

### HTTP-FLV/HLS 模式

```bash
ffmpeg \
  -f avfoundation \
  -framerate 30 \
  -i "0:2" \
  -c:v libx264 \
  -preset veryfast \
  -tune zerolatency \
  -g 25 \
  -vf scale=1920x1080 \
  -r 30 \
  -x264-params keyint=25:min-keyint=25:scenecut=0:bframes=0:ref=1:no-mbtree:rc-lookahead=0 \
  -b:v 1800k \
  -maxrate 1800k \
  -bufsize 3600k \
  -c:a aac \
  -b:a 128k \
  -ar 44100 \
  -ac 2 \
  -f flv \
  "rtmp://127.0.0.1:1935/live/camera_2cd2adb83512f9d4?secret=UQyXemwV81qnNkuXQSp2eo5txJM35PZr"
```

## 🔄 数据流详细路径

### WebRTC 输出链路

```
┌─────────────────┐
│  本地摄像头设备  │
│  (macOS/Linux)  │
└────────┬────────┘
         │
         │ avfoundation/v4l2
         ▼
┌─────────────────┐
│   FFmpeg 输入   │
│  -i "0:2"       │
└────────┬────────┘
         │
         │ 原始视频 + 音频
         ▼
┌─────────────────┐
│  FFmpeg 编码    │
│  H.264 + AAC    │
│  (WebRTC 参数)  │
└────────┬────────┘
         │
         │ FLV 容器
         ▼
┌─────────────────┐
│  RTMP 推流      │
│  rtmp://...     │
└────────┬────────┘
         │
         │ RTMP 协议
         ▼
┌─────────────────┐
│  ZLMediaKit     │
│  RTMP 接收      │
│  (端口 1935)    │
└────────┬────────┘
         │
         │ 解析 FLV
         ▼
┌─────────────────┐
│  ZLM 内部流     │
│  H.264 + AAC    │
└────────┬────────┘
         │
         │ ⚠️ 需要转码
         ▼
┌─────────────────┐
│  ZLM 音频转码   │
│  AAC → Opus     │
│  (transcode)    │
└────────┬────────┘
         │
         │ WebRTC SDP
         ▼
┌─────────────────┐
│  前端 WebRTC    │
│  ZLMRTCClient   │
└────────┬────────┘
         │
         │ RTP 数据包
         ▼
┌─────────────────┐
│  浏览器解码     │
│  H.264 + Opus   │
└─────────────────┘
```

### HTTP-FLV 输出链路

```
┌─────────────────┐
│  本地摄像头设备  │
└────────┬────────┘
         │
         ▼
┌─────────────────┐
│  FFmpeg 编码    │
│  H.264 + AAC    │
│  (FLV/HLS 参数) │
└────────┬────────┘
         │
         ▼
┌─────────────────┐
│  RTMP 推流      │
│  → ZLM          │
└────────┬────────┘
         │
         ▼
┌─────────────────┐
│  ZLM 内部流     │
│  H.264 + AAC    │
└────────┬────────┘
         │
         │ 直接导出
         ▼
┌─────────────────┐
│  ZLM FLV 输出   │
│  HTTP-FLV       │
└────────┬────────┘
         │
         ▼
┌─────────────────┐
│  前端 FLV 播放  │
│  flv.js         │
└─────────────────┘
```

### HLS 输出链路

```
┌─────────────────┐
│  本地摄像头设备  │
└────────┬────────┘
         │
         ▼
┌─────────────────┐
│  FFmpeg 编码    │
│  H.264 + AAC    │
└────────┬────────┘
         │
         ▼
┌─────────────────┐
│  RTMP 推流      │
│  → ZLM          │
└────────┬────────┘
         │
         ▼
┌─────────────────┐
│  ZLM 内部流     │
│  H.264 + AAC    │
└────────┬────────┘
         │
         │ 生成切片
         ▼
┌─────────────────┐
│  ZLM HLS 输出   │
│  .m3u8 + .ts    │
└────────┬────────┘
         │
         ▼
┌─────────────────┐
│  前端 HLS 播放  │
│  hls.js         │
└─────────────────┘
```

## 🔑 关键参数说明

### 视频编码参数

| 参数 | WebRTC | HTTP-FLV/HLS | 说明 |
|------|--------|--------------|------|
| 编码器 | libx264 | libx264 | H.264 编码 |
| 预设 | ultrafast | veryfast | 编码速度 |
| 调优 | zerolatency | zerolatency | 低延迟 |
| GOP | 10 | 25 | 关键帧间隔 |
| 分辨率 | 1280x720 | 1920x1080 | 输出分辨率 |
| 帧率 | 30 | 30 | 输出帧率 |
| B 帧 | 0 | 0 | 无 B 帧（低延迟） |
| 参考帧 | 1 | 1 | 单参考帧（低延迟） |

### 音频编码参数

| 参数 | WebRTC | HTTP-FLV/HLS | 说明 |
|------|--------|--------------|------|
| 编码器 | AAC | AAC | 音频编码 |
| 码率 | 128 kbps | 128 kbps | 音频码率 |
| 采样率 | 48 kHz | 44.1 kHz | 采样率 |
| 声道 | 2 (立体声) | 2 (立体声) | 声道数 |

**注意**：
- WebRTC 使用 48kHz 采样率（WebRTC 标准）
- HTTP-FLV/HLS 使用 44.1kHz 采样率（标准音频采样率）
- 都使用 AAC 编码（FLV 容器兼容）

## ⚠️ 已知问题

### WebRTC 音频问题

**问题**：ZLM 主分支不支持 AAC → Opus 转码

**影响**：
- WebRTC 输出时音频轨道被标记为 `inactive`
- 前端无法接收到音频数据

**解决方案**：
- 切换到 ZLM `transcode` 分支
- 或使用 ZLM 专业版

## 📊 数据流统计

### 推流数据
- **协议**：RTMP
- **容器**：FLV
- **视频**：H.264, 1280x720/1920x1080, 30fps
- **音频**：AAC, 48kHz/44.1kHz, 立体声
- **总码率**：约 2-3 Mbps（视频 + 音频）

### ZLM 处理
- **接收**：RTMP (端口 1935)
- **内部格式**：H.264 + AAC
- **输出协议**：WebRTC / HTTP-FLV / HLS

### 前端播放
- **WebRTC**：H.264 + Opus（需要 ZLM 转码）
- **HTTP-FLV**：H.264 + AAC
- **HLS**：H.264 + AAC

## 🔧 配置位置

### 本地摄像头配置
- 文件：`configs/config.yaml` 或配置文件
- 配置项：
  - `local_camera.webrtc.*` - WebRTC 模式配置
  - `local_camera.flv_hls.*` - HTTP-FLV/HLS 模式配置
  - `local_camera.default_*` - 默认配置

### ZLM 配置
- 文件：`configs/zlm_config.ini`
- 配置项：
  - `rtmp.port` - RTMP 端口（默认 1935）
  - `rtc.*` - WebRTC 配置
  - `protocol.enable_audio` - 音频启用

## 📝 总结

**本地摄像头数据链路**：
1. **输入**：本地摄像头设备（avfoundation/v4l2）
2. **编码**：FFmpeg 转码（H.264 + AAC）
3. **推流**：RTMP/FLV 推送到 ZLM
4. **处理**：ZLM 接收并转换为多种输出协议
5. **播放**：前端根据 `output_protocol` 选择播放器

**关键点**：
- 所有模式都推流 RTMP/FLV 到 ZLM
- 编码参数根据 `output_protocol` 调整
- WebRTC 需要 ZLM 转码（AAC → Opus），需要 `transcode` 分支

