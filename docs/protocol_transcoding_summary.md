# 协议转码情况汇总表

## 输入协议 → ZLM 链路汇总

| 协议/网关 | 输入协议 | 是否需要 FFmpeg 转码 | 推给 ZLM 的格式 | 典型链路 | 备注 |
|---------|---------|-------------------|---------------|---------|------|
| **RTSP** | `rtsp://...` | ✅ **需要** | RTSP (HTTP-FLV/HLS)<br>RTMP/FLV (WebRTC) | `RTSP → FFmpeg → RTSP/RTMP → ZLM` | WebRTC 模式已改为 RTMP/FLV 推流，解决解码问题 |
| **RTMP** | `rtmp://...` | ❌ **不需要** | RTMP | `RTMP → 直通 → ZLM` | 标准 H.264+AAC 可直接推入 |
| **HTTP-FLV** | `http://.../xxx.flv` | ⚠️ **可选** | RTMP/FLV | `HTTP-FLV → FFmpeg(可选) → RTMP → ZLM` | 标准流可直通，有兼容性问题时转码 |
| **HLS** | `.m3u8 + .ts/.m4s` | ❌ **通常不需要** | HLS 文件/段 | `HLS → 文件代理 → ZLM` | 直接作为 HLS 源暴露，除非需要重编码 |
| **DASH** | `.mpd + .m4s` | ✅ **需要** | RTSP/RTMP | `DASH → FFmpeg → RTSP/RTMP → ZLM` | 需要 FFmpeg 拉取并重封装 |
| **QUIC** | QUIC 流 | ✅ **需要** | RTSP/RTMP | `QUIC → FFmpeg → RTSP/RTMP → ZLM` | 实验性协议，需转码 |
| **本地摄像头** | 系统摄像头 | ✅ **需要** | RTMP/FLV | `Camera → FFmpeg → RTMP → ZLM` | macOS: avfoundation, Linux: v4l2 |
| **ONVIF** | ONVIF 协议 | ✅ **需要** | RTSP/RTMP | `ONVIF → 发现RTSP URL → RTSPGateway → FFmpeg → RTSP/RTMP → ZLM` | 本质是 RTSP 流 |
| **ISAPI** | ISAPI 协议 | ✅ **需要** | RTSP/RTMP | `ISAPI → 发现RTSP URL → RTSPGateway → FFmpeg → RTSP/RTMP → ZLM` | 本质是 RTSP 流 |
| **Dahua** | 大华协议 | ✅ **需要** | RTSP/RTMP | `Dahua → 发现RTSP URL → RTSPGateway → FFmpeg → RTSP/RTMP → ZLM` | 本质是 RTSP 流 |
| **PSIA** | PSIA 协议 | ✅ **需要** | RTSP/RTMP | `PSIA → 发现RTSP URL → RTSPGateway → FFmpeg → RTSP/RTMP → ZLM` | 本质是 RTSP 流 |
| **NDI** | NDI 流 | ✅ **需要** | RTSP/RTMP | `NDI → FFmpeg → RTSP/RTMP → ZLM` | 需要 FFmpeg 转码 |

## ZLM 输出协议（无需转码）

| 输出协议 | 前端组件 | 说明 |
|---------|---------|------|
| **HTTP-FLV** | `FLVPlayer` | ZLM 内部直接导出，无需 FFmpeg |
| **HLS** | `HLSPlayer` | ZLM 内部直接导出，无需 FFmpeg |
| **WebRTC** | `WebRTCPlayer` | ZLM 内部直接导出，无需 FFmpeg |
| **RTSP** | - | ZLM 原生支持，可直接播放 |
| **RTMP** | - | ZLM 原生支持，可直接推流 |

## 关键链路说明

### 1. RTSP + WebRTC（已修复）

**之前（有问题）：**
```
RTSP 源 → FFmpeg → RTSP 推给 ZLM → ZLM 从 RTSP schema 导出 WebRTC
```
- ❌ 浏览器收包但解码帧数=0

**现在（已修复）：**
```
RTSP 源 → FFmpeg(H.264+AAC, FLV) → RTMP 推给 ZLM → ZLM 从 RTMP schema 导出 WebRTC
```
- ✅ 浏览器正常解码，有画面

### 2. 本地摄像头 + WebRTC（参考标准）

```
本地摄像头 → FFmpeg(H.264+AAC, FLV) → RTMP 推给 ZLM → ZLM 导出 WebRTC
```
- ✅ 已知正常工作

### 3. HTTP-FLV + WebRTC

```
HTTP-FLV 源 → FFmpeg(可选) → RTMP 推给 ZLM → ZLM 导出 WebRTC
```
- ✅ 已知正常工作

## 转码参数说明

### WebRTC 模式（低延迟编码）

- **视频编码器**: `libx264`
- **预设**: `veryfast` + `zerolatency`
- **GOP 大小**: 15（小 GOP，降低延迟）
- **关键参数**:
  - `bframes=0`（无 B 帧）
  - `ref=1`（单参考帧）
  - `no-mbtree`（禁用宏块树）
  - `rc-lookahead=0`（无前瞻）
  - `scenecut=0`（禁用场景切换检测）
- **分辨率**: 1920x1080（1080p）
- **帧率**: 30fps
- **音频编码器**: AAC
- **音频参数**: `-b:a 128k -ar 44100 -ac 2`

### HTTP-FLV / HLS 模式（标准编码）

- **视频编码器**: `libx264`
- **预设**: `veryfast`
- **GOP 大小**: 25
- **分辨率**: 1920x1080
- **帧率**: 30fps
- **音频编码器**: AAC
- **音频参数**: `-b:a 128k -ar 44100 -ac 2`

## 总结

1. **需要转码的协议**：RTSP、DASH、QUIC、本地摄像头、ONVIF/ISAPI/Dahua/PSIA（本质是 RTSP）、NDI
2. **可直通的协议**：标准 RTMP、标准 HTTP-FLV、标准 HLS
3. **ZLM 输出协议**：全部由 ZLM 内部处理，无需 FFmpeg
4. **关键修复**：RTSP + WebRTC 已改为 RTMP/FLV 推流，与本地摄像头链路保持一致

