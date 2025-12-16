# 网关逻辑完整总结

## 📋 核心概念

### 直接代理 vs 转码推流

**直接代理（Direct Proxy）**：
- 调用 ZLM API（`AddRTSPStream`/`AddStreamProxy`/`AddRTMPStream`）
- ZLM 自己去拉流，**不推流**
- **无推流格式**（因为不推流）
- **性能最优**：无转码开销，无推流开销

**转码推流（Transcoding Push）**：
- 使用 FFmpeg 转码并推流到 ZLM
- **有推流格式**（RTSP 或 RTMP/FLV）
- 需要 FFmpeg 进程，有转码开销

## 🔍 各网关详细逻辑

### 1. RTSP Gateway

#### 直接代理条件

**条件**：
- `output_protocol == "http-flv" || "hls"`
- **且** 源流音频编码是 `aac`

**实现**：
```cpp
zlm_client_->AddRTSPStream(target_app, target_stream, source_url);
```

**流程**：
```
RTSP 源流 → ZLM 自己拉流 → ZLM 内部处理 → 前端播放
```

**特点**：
- ✅ 不推流（ZLM 自己拉流）
- ✅ 无转码开销
- ✅ 无 FFmpeg 进程
- ✅ 性能最优

#### 转码推流条件

**条件 1**：`output_protocol == "webrtc"`
- **原因**：需要特定的编码参数（GOP=15, zerolatency, bframes=0 等）
- **推流格式**：RTMP/FLV
- **实现**：FFmpeg 转码并推 RTMP/FLV 到 ZLM

**条件 2**：`output_protocol == "http-flv" || "hls"` **且** 音频不是 AAC
- **原因**：需要将音频转换为 AAC（FLV/HLS 需要 AAC）
- **推流格式**：RTSP（当前）→ RTMP/FLV（未来）
- **实现**：FFmpeg 转码并推 RTSP 到 ZLM

**条件 3**：直接代理失败或流未活跃
- **原因**：回退到转码模式
- **推流格式**：RTSP（当前）→ RTMP/FLV（未来）
- **实现**：FFmpeg 转码并推 RTSP 到 ZLM

**流程**：
```
RTSP 源流 → FFmpeg 转码 → 推流到 ZLM → ZLM 内部处理 → 前端播放
```

**特点**：
- ⚠️ 需要推流（FFmpeg 推流到 ZLM）
- ⚠️ 有转码开销
- ⚠️ 有 FFmpeg 进程

### 2. HTTP-FLV Gateway

#### 直接代理条件

**条件**：
- 源流音频编码是 `aac`

**实现**：
```cpp
zlm_client_->AddStreamProxy(target_app, target_stream, source_url);
```

**流程**：
```
HTTP-FLV 源流 → ZLM 自己拉流 → ZLM 内部处理 → 前端播放
```

**特点**：
- ✅ 不推流（ZLM 自己拉流）
- ✅ 无转码开销
- ✅ 无 FFmpeg 进程
- ✅ 性能最优

#### 转码推流条件

**条件**：音频不是 AAC
- **原因**：需要将音频转换为 AAC（FLV 需要 AAC）
- **推流格式**：RTSP（当前）→ RTMP/FLV（未来）
- **转码策略**：视频复制（`-c:v copy`），音频转 AAC
- **实现**：FFmpeg 转码并推 RTSP 到 ZLM

**流程**：
```
HTTP-FLV 源流 → FFmpeg 转码（视频复制，音频转 AAC）→ 推流到 ZLM → ZLM 内部处理 → 前端播放
```

**特点**：
- ⚠️ 需要推流（FFmpeg 推流到 ZLM）
- ⚠️ 有转码开销（仅音频转码）
- ⚠️ 有 FFmpeg 进程

### 3. HLS Gateway

#### 直接代理条件

**条件**：所有情况

**实现**：
```cpp
zlm_client_->AddStreamProxy(target_app, target_stream, source_url);
```

**流程**：
```
HLS 源流（.m3u8 + .ts）→ ZLM 自己拉流 → ZLM 内部处理 → 前端播放
```

**特点**：
- ✅ 不推流（ZLM 自己拉流）
- ✅ 无转码开销
- ✅ 无 FFmpeg 进程
- ✅ 性能最优
- ✅ **所有情况都直接代理，无需转码**

### 4. RTMP Gateway

#### 直接代理条件

**条件**：所有情况

**实现**：
```cpp
zlm_client_->AddRTMPStream(target_app, target_stream, source_url);
```

**流程**：
```
RTMP 源流 → ZLM 自己拉流 → ZLM 内部处理 → 前端播放
```

**特点**：
- ✅ 不推流（ZLM 自己拉流）
- ✅ 无转码开销
- ✅ 无 FFmpeg 进程
- ✅ 性能最优
- ✅ **所有情况都直接代理，无需转码**

### 5. DASH Gateway

#### 转码推流条件

**条件**：所有情况（必须转码）

**原因**：
- ❌ **DASH 不是 ZLM 原生支持的协议**
- ZLM 无法直接拉取 DASH 流
- 必须使用 FFmpeg 拉取 DASH 流并转码为 ZLM 支持的格式

**推流格式**：RTSP（当前）→ RTMP/FLV（未来）

**实现**：FFmpeg 转码并推 RTSP 到 ZLM

**流程**：
```
DASH 源流（.mpd + .m4s）→ FFmpeg 拉取并转码 → 推流到 ZLM → ZLM 内部处理 → 前端播放
```

**特点**：
- ⚠️ 需要推流（FFmpeg 推流到 ZLM）
- ⚠️ 有转码开销
- ⚠️ 有 FFmpeg 进程
- ⚠️ **所有情况都必须转码**

### 6. QUIC Gateway

#### 转码推流条件

**条件**：所有情况（必须转码）

**原因**：
- ❌ **QUIC 不是 ZLM 原生支持的协议**
- QUIC 是实验性协议，ZLM 不支持
- 必须使用 FFmpeg 拉取 QUIC 流并转码为 ZLM 支持的格式

**推流格式**：RTSP（当前）→ RTMP/FLV（未来）

**实现**：FFmpeg 转码并推 RTSP 到 ZLM

**流程**：
```
QUIC 源流 → FFmpeg 拉取并转码 → 推流到 ZLM → ZLM 内部处理 → 前端播放
```

**特点**：
- ⚠️ 需要推流（FFmpeg 推流到 ZLM）
- ⚠️ 有转码开销
- ⚠️ 有 FFmpeg 进程
- ⚠️ **所有情况都必须转码**

### 7. Local Camera Gateway

#### 转码推流条件

**条件**：所有情况（必须转码）

**原因**：
- ❌ **摄像头原始输出不是标准流格式**
- macOS: `avfoundation` 输出原始视频帧
- Linux: `v4l2` 输出原始视频帧
- 必须使用 FFmpeg 编码为 H.264+AAC 并推流

**推流格式**：RTMP/FLV（统一使用）

**实现**：FFmpeg 编码并推 RTMP/FLV 到 ZLM

**流程**：
```
摄像头设备 → FFmpeg 编码（H.264+AAC）→ 推 RTMP/FLV 到 ZLM → ZLM 内部处理 → 前端播放
```

**特点**：
- ⚠️ 需要推流（FFmpeg 推流到 ZLM）
- ⚠️ 有转码开销（编码）
- ⚠️ 有 FFmpeg 进程
- ⚠️ **所有情况都必须转码**
- ✅ **统一使用 RTMP/FLV，最稳定**

### 8. 设备发现网关（ONVIF/ISAPI/Dahua/PSIA）

#### 处理流程

**流程**：
1. 设备发现 → 获取 RTSP URL
2. 调用 RTSP Gateway → 按 RTSP Gateway 规则处理

**结果**：
- 如果 RTSP 流音频是 AAC 且 `output_protocol != "webrtc"`：直接代理（不推流）
- 否则：转码推流（推 RTSP 或 RTMP/FLV，按 RTSP Gateway 规则）

## 📊 完整逻辑表

| 网关 | 输入协议 | ZLM 原生支持？ | 直接代理条件 | 转码条件 | 转码时推流格式（当前） | 转码时推流格式（未来） |
|------|---------|---------------|------------|---------|---------------------|---------------------|
| **RTSP Gateway** | RTSP | ✅ 是 | `output_protocol != "webrtc"` **且** 音频是 AAC | `output_protocol == "webrtc"` **或** 音频不是 AAC | RTMP/FLV（WebRTC）<br>RTSP（其他） | RTMP/FLV（全部） |
| **HTTP-FLV Gateway** | HTTP-FLV | ✅ 是 | 音频是 AAC | 音频不是 AAC | RTSP | RTMP/FLV |
| **HLS Gateway** | HLS | ✅ 是 | 所有情况 | 无 | 无 | 无 |
| **RTMP Gateway** | RTMP | ✅ 是 | 所有情况 | 无 | 无 | 无 |
| **DASH Gateway** | DASH | ❌ 否 | 无 | 所有情况（必须转码） | RTSP | RTMP/FLV |
| **QUIC Gateway** | QUIC | ❌ 否 | 无 | 所有情况（必须转码） | RTSP | RTMP/FLV |
| **Local Camera** | 摄像头 | ❌ 否 | 无 | 所有情况（必须转码） | RTMP/FLV | RTMP/FLV |
| **ONVIF/ISAPI/Dahua/PSIA** | 设备协议 | ✅ 是（最终是 RTSP） | 按 RTSP Gateway 规则 | 按 RTSP Gateway 规则 | 按 RTSP Gateway 规则 | 按 RTSP Gateway 规则 |

## 🔄 决策流程图

### RTSP Gateway 决策流程

```
输入：RTSP 源流
  ↓
output_protocol == "webrtc"?
  ├─ 是 → 转码推流 RTMP/FLV（需要特定编码参数）
  └─ 否 → 音频是 AAC?
         ├─ 是 → 尝试直接代理（ZLM 自己拉流）
         │        ├─ 成功 → 直接代理（不推流）
         │        └─ 失败 → 转码推流 RTSP（未来改为 RTMP/FLV）
         └─ 否 → 转码推流 RTSP（未来改为 RTMP/FLV）
```

### HTTP-FLV Gateway 决策流程

```
输入：HTTP-FLV 源流
  ↓
音频是 AAC?
  ├─ 是 → 直接代理（ZLM 自己拉流，不推流）
  └─ 否 → 转码推流 RTSP（未来改为 RTMP/FLV）
```

### DASH/QUIC Gateway 决策流程

```
输入：DASH/QUIC 源流
  ↓
必须转码（ZLM 不支持）
  ↓
转码推流 RTSP（未来改为 RTMP/FLV）
```

### Local Camera Gateway 决策流程

```
输入：摄像头设备
  ↓
必须转码（原始输出不是流格式）
  ↓
转码推流 RTMP/FLV（统一使用）
```

## 🎯 关键要点总结

### 1. 直接代理优先

**原则**：
- 如果 ZLM 原生支持该协议，优先尝试直接代理
- 只有在必要时才转码

**优势**：
- ✅ 无转码开销
- ✅ 无推流开销
- ✅ 无 FFmpeg 进程
- ✅ 性能最优

### 2. 转码推流格式统一

**当前状态**：
- RTSP Gateway WebRTC：推 RTMP/FLV ✅
- RTSP Gateway 其他：推 RTSP ⚠️
- HTTP-FLV Gateway：推 RTSP ⚠️
- DASH/QUIC Gateway：推 RTSP ⚠️
- Local Camera：推 RTMP/FLV ✅

**未来目标**：
- 所有转码都统一推 RTMP/FLV
- 更稳定，已验证

### 3. 转码的主要原因

1. **协议不支持**：DASH、QUIC、摄像头
2. **编码格式不兼容**：音频不是 AAC（FLV/HLS 需要 AAC）
3. **需要特定参数**：WebRTC 需要低延迟参数（GOP=15, zerolatency 等）

### 4. 性能对比

| 方式 | CPU 开销 | 内存开销 | 延迟 | 稳定性 |
|------|---------|---------|------|--------|
| **直接代理** | 最低 | 最低 | 最低 | 最高 |
| **转码推流** | 高 | 中 | 中 | 高 |

## 📝 实施建议

### 短期（保持现状）

1. **保持直接代理优先**：当前实现已经很好
2. **监控转码使用情况**：统计哪些流需要转码

### 中期（统一推流格式）

1. **统一转码推流格式**：所有转码都推 RTMP/FLV
2. **分阶段实施**：先 RTSP Gateway，再其他网关
3. **充分测试**：每个阶段都要全面测试

### 长期（优化转码条件）

1. **智能检测**：检测源流编码参数，如果符合要求则直接代理
2. **动态调整**：根据源流质量动态选择转码参数
3. **性能优化**：优化 FFmpeg 参数，降低转码开销

## 🔍 验证检查清单

### 直接代理验证

- [ ] ZLM API 调用成功
- [ ] 流在 ZLM 中活跃
- [ ] 前端可以正常播放
- [ ] 无 FFmpeg 进程
- [ ] 性能指标正常

### 转码推流验证

- [ ] FFmpeg 进程启动成功
- [ ] 推流到 ZLM 成功
- [ ] 流在 ZLM 中活跃
- [ ] 前端可以正常播放
- [ ] 转码参数正确
- [ ] 性能指标可接受

## 🎯 总结

### 核心逻辑

1. **优先直接代理**：如果 ZLM 原生支持且格式兼容，直接代理
2. **必要时转码**：协议不支持、编码不兼容、需要特定参数时转码
3. **统一推流格式**：所有转码统一推 RTMP/FLV（未来目标）

### 当前状态

- ✅ 直接代理逻辑正确，已实现
- ⚠️ 转码推流格式不统一（RTSP vs RTMP）
- 📋 未来统一为 RTMP/FLV

### 关键区别

- **直接代理**：不推流，ZLM 自己拉流
- **转码推流**：FFmpeg 推流到 ZLM（推 RTSP 或 RTMP/FLV）

