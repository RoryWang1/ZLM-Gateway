# GB28181 功能完整指南

本文档是 GB28181 功能的完整指南，包含架构设计、快速开始、详细实现、部署注意事项等所有内容。

## 目录

1. [概述](#1-概述)
2. [快速开始](#2-快速开始)
3. [GB28181 协议详解](#3-gb28181-协议详解)
4. [架构设计](#4-架构设计)
5. [详细设计](#5-详细设计)
6. [部署注意事项](#6-部署注意事项)
7. [实现步骤](#7-实现步骤)
8. [API 使用示例](#8-api-使用示例)
9. [设备管理功能详解](#9-设备管理功能详解)
10. [常见问题](#10-常见问题)
11. [参考资源](#11-参考资源)

---

## 1. 概述

### 0. 重新设计方案总结

**核心原则**：充分利用 ZLM 原生功能，只实现 ZLM 不支持的部分

#### 功能分工

| 功能 | 实现方 | 说明 |
|------|--------|------|
| **RTP 服务器管理** | ZLM 原生 | 直接调用 ZLM API（openRtpServer/closeRtpServer） |
| **RTP 流接收和处理** | ZLM 原生 | ZLM 的 GB28181Process 已实现 PS 流解码等 |
| **流格式转换** | ZLM 原生 | HTTP-FLV/HLS/RTSP/WebRTC 自动转换 |
| **流状态查询** | ZLM 原生 | 直接调用 ZLM API（getMediaList/getStreamInfo） |
| **SIP 服务器** | Gateway 实现 | ZLM 不支持，需要 Gateway 实现 |
| **设备管理** | Gateway 实现 | ZLM 不支持，需要 Gateway 实现 |
| **SIP 信令处理** | Gateway 实现 | ZLM 不支持，需要 Gateway 实现 |

#### 开发重点

1. **ZLMClient 扩展**（阶段1）
   - 封装 ZLM 的 RTP 服务器 API
   - 提供易用的 C++ 接口
   - **不重复实现 RTP 服务器逻辑**

2. **SIP 服务器实现**（阶段2）
   - 使用成熟的 SIP 库（osip2/eXosip2）
   - 处理 REGISTER/INVITE/BYE/MESSAGE
   - **这是 Gateway 的核心功能**

3. **GB28181Gateway 核心**（阶段3）
   - 整合 SIP 服务器和 ZLM RTP 服务器
   - 实现设备管理和流管理协调
   - **充分利用 ZLM 功能，不重复实现**

#### 关键设计决策

- ✅ **直接调用 ZLM API**：RTP 服务器创建/关闭直接调用 ZLM API
- ✅ **流状态查询使用 ZLM**：不维护自己的流状态，直接查询 ZLM
- ✅ **只实现 ZLM 不支持的功能**：SIP 服务器、设备管理、SIP 信令处理
- ❌ **不重复实现**：RTP 流接收、PS 流解码、流格式转换等

详细实现步骤见第 7 节。

---

### 1.0 ZLM 原生 GB28181 支持情况

**重要**：在实现 GB28181Gateway 之前，必须了解 ZLM 原生支持哪些 GB28181 功能，避免重复造轮子。

#### ZLM 原生支持的 GB28181 功能

ZLM 已经原生支持以下 GB28181 功能，**我们只需要调用 ZLM 的 API**：

1. **RTP 服务器管理**（完整支持）
   - ✅ `openRtpServer`: 创建 RTP 服务器，接收 GB28181 RTP 流
   - ✅ `closeRtpServer`: 关闭 RTP 服务器
   - ✅ `listRtpServer`: 获取 RTP 服务器列表
   - ✅ `getRtpInfo`: 获取 RTP 推流信息（连接IP、端口等）
   - ✅ `updateRtpServerSSRC`: 更新 RTP 服务器 SSRC
   - ✅ `openRtpServerMultiplex`: 创建多路复用 RTP 服务器

2. **RTP 流接收和处理**（完整支持）
   - ✅ RTP over UDP/TCP 接收
   - ✅ PS 流（MPEG-PS）解码（GB28181 常用格式）
   - ✅ TS 流（MPEG-TS）解码
   - ✅ H.264/H.265 视频解码
   - ✅ G.711/G.722/AAC/Opus 音频解码
   - ✅ RTP 包排序和重组
   - ✅ 流超时检测

3. **TCP 模式支持**（完整支持）
   - ✅ `tcp_mode=0`: UDP 模式（默认）
   - ✅ `tcp_mode=1`: TCP 被动模式（服务器监听）
   - ✅ `tcp_mode=2`: TCP 主动模式（主动连接到设备）

4. **流输出**（完整支持）
   - ✅ 自动转换为 HTTP-FLV、HLS、RTSP、WebRTC 等格式
   - ✅ 支持多路播放

#### ZLM 不支持的 GB28181 功能（需要 Gateway 实现）

ZLM **不提供**以下功能，需要我们在 Gateway 层实现：

1. **SIP 服务器**（需要 Gateway 实现）
   - ❌ SIP REGISTER 处理（设备注册）
   - ❌ SIP INVITE 处理（视频点播请求）
   - ❌ SIP BYE 处理（停止推流）
   - ❌ SIP MESSAGE 处理（心跳、设备信息查询）
   - ❌ SIP 事务管理
   - ❌ SIP 消息解析和构造

2. **设备管理**（需要 Gateway 实现）
   - ❌ 设备注册管理
   - ❌ 设备状态跟踪（在线/离线）
   - ❌ 设备心跳检测
   - ❌ 设备信息存储（IP、认证信息等）

3. **设备认证**（需要 Gateway 实现）
   - ❌ Digest 认证（SIP 消息认证）
   - ❌ 设备认证信息管理

4. **SIP 信令处理**（需要 Gateway 实现）
   - ❌ SDP 解析和构造
   - ❌ SIP 消息路由
   - ❌ NAT 穿透处理

#### 我们的实现策略

**充分利用 ZLM 原生功能**：
- ✅ 直接使用 ZLM 的 `openRtpServer` API 创建 RTP 服务器
- ✅ 直接使用 ZLM 的 `closeRtpServer` API 关闭 RTP 服务器
- ✅ 直接使用 ZLM 的 `getRtpInfo` API 查询 RTP 流信息
- ✅ 依赖 ZLM 的 RTP 流接收和处理能力

**Gateway 层只实现 ZLM 不支持的功能**：
- ✅ 实现 SIP 服务器（处理设备注册、心跳、控制）
- ✅ 实现设备管理（设备列表、状态跟踪）
- ✅ 实现 SIP 信令处理（INVITE、BYE、MESSAGE）
- ✅ 实现设备认证（Digest 认证）

**架构分工**：
```
┌─────────────────────────────────────┐
│  GB28181Gateway (我们实现)          │
│  - SIP 服务器                       │
│  - 设备管理                         │
│  - SIP 信令处理                      │
└──────────────┬──────────────────────┘
               │ 调用 ZLM API
               ▼
┌─────────────────────────────────────┐
│  ZLMediaKit (原生支持)               │
│  - RTP 服务器管理                    │
│  - RTP 流接收和处理                   │
│  - 流格式转换（HTTP-FLV/HLS等）      │
└─────────────────────────────────────┘
```

### 1.1 GB28181 的本质定位

**重要说明**：GB28181 是一个**完整的视频监控联网系统标准**，它既包含设备发现协议，也包含流传输协议。

#### GB28181 的组成

GB28181 标准包含三个主要部分：

1. **设备发现和控制协议**：SIP（Session Initiation Protocol）
   - 设备注册：SIP REGISTER
   - 设备控制：SIP INVITE（视频点播）、SIP BYE（停止推流）
   - 设备心跳：SIP MESSAGE
   - 设备查询：SIP MESSAGE（设备信息查询）

2. **流传输协议**：RTP（Real-time Transport Protocol）
   - 音视频数据传输：RTP over UDP/TCP
   - 支持 PS 流（Program Stream，GB28181 常用格式）
   - 支持 H.264/H.265 视频编码
   - 支持 G.711/AAC 音频编码

3. **设备管理协议**：通过 SIP 实现
   - 设备注册管理
   - 设备状态管理
   - 设备认证管理

#### 与 ONVIF 的对比

| 特性 | GB28181 | ONVIF |
|------|---------|-------|
| **标准类型** | 完整的视频监控系统标准 | 设备发现和配置协议 |
| **设备发现** | SIP REGISTER（设备主动注册） | WS-Discovery（Gateway主动扫描） |
| **设备控制** | SIP INVITE/BYE/MESSAGE | ONVIF SOAP API |
| **流传输协议** | RTP（GB28181 标准的一部分） | RTSP（标准协议，非 ONVIF 定义） |
| **流获取方式** | 设备主动推送 RTP 流 | Gateway 主动拉取 RTSP 流 |
| **工作模式** | 被动接收（服务器等待设备推送） | 主动拉取（Gateway 主动拉取） |
| **协议栈** | SIP + RTP（一体化） | WS-Discovery + SOAP + RTSP（分离） |

**关键区别**：

1. **协议完整性**：
   - GB28181：包含完整的协议栈（SIP 信令 + RTP 流传输），是一个完整的系统标准
   - ONVIF：主要是设备发现和配置协议，流传输使用标准的 RTSP（不是 ONVIF 定义的）

2. **工作模式**：
   - GB28181：**被动接收模式**（设备主动注册和推送流）
   - ONVIF：**主动拉取模式**（Gateway 主动发现设备和拉取流）

3. **协议栈**：
   - GB28181：SIP（信令）+ RTP（流传输），一体化设计
   - ONVIF：WS-Discovery（发现）+ SOAP（控制）+ RTSP（流传输），分离设计

#### 在项目中的定位

在我们的项目中，GB28181Gateway 被归类为"设备发现 Gateway"，但实际上它包含两个功能：

1. **设备发现功能**（类似 ONVIF）：
   - 通过 SIP REGISTER 发现设备
   - 维护设备列表和状态
   - 提供设备管理接口

2. **流管理功能**（类似 RTSP Gateway）：
   - 管理 RTP 流的接收
   - 创建/关闭 RTP 服务器
   - 跟踪流状态

**为什么归类为设备发现 Gateway？**
- 为了与其他设备发现 Gateway（ONVIF、ISAPI等）保持一致
- 前端可以统一处理不同协议的设备
- 提供统一的设备管理接口

**实际上**：
- GB28181 = 设备发现协议（SIP）+ 流传输协议（RTP）
- ONVIF = 设备发现协议（WS-Discovery）+ 流传输协议（RTSP，标准协议）

### 1.2 与其他设备发现Gateway的对比

| Gateway | 发现协议 | 控制协议 | 流传输协议 | 流获取方式 | 流推送方式 |
|---------|---------|---------|-----------|-----------|-----------|
| ONVIF | WS-Discovery | ONVIF SOAP API | RTSP（标准协议） | Gateway主动拉RTSP流 | Gateway主动拉取 |
| ISAPI | HTTP扫描 | ISAPI HTTP API | RTSP（标准协议） | Gateway主动拉RTSP流 | Gateway主动拉取 |
| GB28181 | SIP REGISTER | SIP INVITE/BYE | RTP（GB28181标准） | 设备主动推RTP流 | 设备主动推送 |

**关键区别**：
- GB28181 是**被动接收**（设备推送RTP流），包含完整的协议栈
- 其他Gateway是**主动拉取**（Gateway拉取RTSP流），流传输使用标准RTSP协议
- GB28181 需要**SIP服务器**处理信令（内置在Gateway中）
- 其他Gateway直接调用设备的HTTP/SOAP API

---

## 2. 快速开始

### 2.1 配置 ZLM

确保 `configs/zlm_config.ini` 中的 `[rtp_proxy]` 配置段已正确配置：

```ini
[rtp_proxy]
dumpDir=                                    # RTP 调试文件导出目录（可选）
gop_cache=1                                 # 开启 GOP 缓存
h264_pt=98                                  # H264 负载类型
h265_pt=99                                  # H265 负载类型
merge_frame=1                               # 合并帧
opus_pt=100                                 # Opus 负载类型
port=10000                                  # GB28181 RTP 接收端口（默认）
port_range=30000-35000                      # 动态端口范围
ps_pt=96                                    # PS 流负载类型（GB28181 常用）
rtp_g711_dur_ms=100                         # G.711 音频包时长（ms）
timeoutSec=15                               # 流超时时间（秒）
udp_recv_socket_buffer=8388608             # UDP 接收缓冲区（8MB）
```

**关键配置项说明**：
- `port=10000`：默认 RTP 接收端口，GB28181 设备会向此端口推送 RTP 流
- `port_range=30000-35000`：动态创建 RTP 服务器时的端口范围
- `ps_pt=96`：PS 流负载类型，GB28181 常用格式
- `rtp_g711_dur_ms=100`：G.711 音频包时长，符合 GB28181-2016 标准

### 2.2 配置 Gateway

在 `configs/config.json` 中添加 GB28181 配置：

```json
{
  "gb28181": {
    "enabled": true,
    "sip_server": {
      "local_ip": "0.0.0.0",
      "local_port": 5060,
      "server_id": "34020000002000000001",
      "domain": "3402000000",
      "register_expires": 3600,
      "heartbeat_interval": 60
    },
    "rtp_server": {
      "default_port": 0,
      "port_range": "30000-35000",
      "default_tcp_mode": 0,
      "default_enable_rtcp": false
    },
    "device": {
      "heartbeat_timeout": 300,
      "auto_remove_offline": false
    },
    "stream_validation": {
      "check_interval_ms": 3000,
      "check_timeout_ms": 10000,
      "max_check_attempts": 10
    },
    "auto_cleanup": {
      "enabled": true,
      "timeout_seconds": 300
    }
  }
}
```

### 2.3 启动服务

```bash
# 1. 启动 ZLMediaKit
./scripts/setup/start_zlmediakit.sh

# 2. 启动 Gateway
./bin/gateway_manager
```

### 2.4 验证

```bash
# 检查 ZLM 是否运行
curl http://localhost:8081/index/api/getServerConfig?secret=YOUR_SECRET

# 检查 Gateway 是否运行
curl http://localhost:8080/api/v1/status

# 发现设备（设备会自动通过SIP REGISTER注册）
curl http://localhost:8080/api/v1/gb28181/devices
```

### 2.5 使用示例

#### 方式 1：通过 HTTP API 启动流

```bash
# 启动设备流
curl -X POST "http://localhost:8080/api/v1/gb28181/streams" \
  -H "Content-Type: application/json" \
  -d '{
    "source_url": "gb28181://34020000001320000001/1",
    "target_app": "rtp",
    "target_stream": "34020000001320000001_1"
  }'

# 响应包含RTP服务器信息和播放地址
{
  "success": true,
  "data": {
    "stream_id": "rtp/34020000001320000001_1",
    "rtp_server": {
      "port": 30000,
      "tcp_mode": 0,
      "local_ip": "192.168.1.100"
    },
    "play_urls": {
      "http_flv": "http://localhost:8081/rtp/34020000001320000001_1.flv",
      "rtsp": "rtsp://localhost:5554/rtp/34020000001320000001_1",
      "hls": "http://localhost:8081/rtp/34020000001320000001_1/hls.m3u8"
    }
  }
}
```

#### 方式 2：使用 FFmpeg 模拟推流测试

```bash
# 使用 FFmpeg 模拟 PS 流推送到 ZLM
ffmpeg -re -i test.mp4 \
  -vcodec h264 \
  -acodec aac \
  -f rtp_mpegts \
  rtp://127.0.0.1:10000
```

---

## 3. GB28181 协议详解

### 3.1 GB28181 协议栈

GB28181 是一个完整的视频监控联网系统标准，包含以下协议层：

```
┌─────────────────────────────────────┐
│  应用层                              │
│  - 设备注册、心跳、控制              │
│  - 视频点播、停止推流                │
└──────────────┬──────────────────────┘
               │
┌──────────────▼──────────────────────┐
│  SIP 信令层（设备发现和控制）        │
│  - REGISTER: 设备注册                │
│  - INVITE: 视频点播请求               │
│  - BYE: 停止推流                      │
│  - MESSAGE: 心跳、设备信息查询        │
└──────────────┬──────────────────────┘
               │
┌──────────────▼──────────────────────┐
│  RTP 流传输层（音视频数据传输）       │
│  - RTP over UDP/TCP                  │
│  - PS 流格式（GB28181 常用）          │
│  - H.264/H.265 视频                  │
│  - G.711/AAC 音频                    │
└─────────────────────────────────────┘
```

**对比：ONVIF 协议栈**

```
┌─────────────────────────────────────┐
│  应用层                              │
│  - 设备发现、配置、控制              │
└──────────────┬──────────────────────┘
               │
┌──────────────▼──────────────────────┐
│  WS-Discovery（设备发现）            │
│  - UDP 多播扫描                       │
└──────────────┬──────────────────────┘
               │
┌──────────────▼──────────────────────┐
│  ONVIF SOAP API（设备控制）          │
│  - HTTP + SOAP                       │
│  - 获取 RTSP 流地址                  │
└──────────────┬──────────────────────┘
               │
┌──────────────▼──────────────────────┐
│  RTSP（流传输，标准协议）            │
│  - RTSP 控制协议                     │
│  - RTP 数据传输                      │
└─────────────────────────────────────┘
```

**关键区别**：
- GB28181：SIP + RTP 是**一体化设计**，都是 GB28181 标准的一部分
- ONVIF：WS-Discovery + SOAP 是 ONVIF 定义的，但 RTSP 是**标准协议**（不是 ONVIF 定义的）

### 3.2 GB28181 工作流程（重新设计）

#### 完整的 GB28181 流程

```
1. 设备发现阶段（SIP REGISTER）
   ┌─────────────┐
   │ GB28181设备 │
   └──────┬──────┘
          │ SIP REGISTER
          ▼
   ┌─────────────────┐
   │ SIP服务器       │ (Gateway实现)
   │ - 解析REGISTER  │
   │ - 提取设备信息   │
   └──────┬──────────┘
          │
          ▼
   ┌─────────────────┐
   │ 设备管理        │ (Gateway实现)
   │ - 添加到devices_│
   │ - 设置online=true│
   └──────┬──────────┘
          │
          ▼
   ┌─────────────────┐
   │ 发送200 OK      │ (Gateway实现)
   └─────────────────┘

2. 设备心跳阶段（SIP MESSAGE）
   ┌─────────────┐
   │ GB28181设备 │
   └──────┬──────┘
          │ SIP MESSAGE (Keepalive)
          ▼
   ┌─────────────────┐
   │ SIP服务器       │ (Gateway实现)
   │ - 解析MESSAGE   │
   └──────┬──────────┘
          │
          ▼
   ┌─────────────────┐
   │ 设备管理        │ (Gateway实现)
   │ - 更新last_seen │
   └──────┬──────────┘
          │
          ▼
   ┌─────────────────┐
   │ 发送200 OK      │ (Gateway实现)
   └─────────────────┘

3. 视频点播阶段（SIP INVITE + RTP）
   ┌─────────────┐
   │ 客户端API   │
   └──────┬──────┘
          │ POST /api/v1/gb28181/streams
          ▼
   ┌─────────────────┐
   │ GB28181Gateway  │ (Gateway实现)
   │ - 验证设备      │
   │ - 调用ZLMClient │
   └──────┬──────────┘
          │
          ▼
   ┌─────────────────┐
   │ ZLMClient       │ (Gateway封装)
   │ OpenRtpServer() │
   └──────┬──────────┘
          │ HTTP API调用
          ▼
   ┌─────────────────┐
   │ ZLMediaKit      │ (ZLM原生)
   │ - 创建RTP服务器 │
   │ - 分配端口      │
   └──────┬──────────┘
          │ 返回端口号
          ▼
   ┌─────────────────┐
   │ GB28181Gateway  │ (Gateway实现)
   │ - 发送SIP INVITE│
   └──────┬──────────┘
          │ SIP INVITE
          ▼
   ┌─────────────┐
   │ GB28181设备 │
   └──────┬──────┘
          │ 200 OK (SDP)
          ▼
   ┌─────────────────┐
   │ GB28181设备     │
   │ - 开始推送RTP流 │
   └──────┬──────────┘
          │ RTP流
          ▼
   ┌─────────────────┐
   │ ZLMediaKit      │ (ZLM原生)
   │ - 接收RTP流     │
   │ - PS流解码      │
   │ - 音视频解码    │
   │ - 格式转换      │
   └──────┬──────────┘
          │
          ▼
   ┌─────────────────┐
   │ 播放地址        │
   │ HTTP-FLV/HLS等  │
   └─────────────────┘

4. 停止推流阶段（SIP BYE）
   ┌─────────────┐
   │ 客户端API   │
   └──────┬──────┘
          │ DELETE /api/v1/gb28181/streams
          ▼
   ┌─────────────────┐
   │ GB28181Gateway  │ (Gateway实现)
   │ - 发送SIP BYE  │
   └──────┬──────────┘
          │ SIP BYE
          ▼
   ┌─────────────┐
   │ GB28181设备 │
   └──────┬──────┘
          │ 200 OK
          ▼
   ┌─────────────────┐
   │ GB28181Gateway  │ (Gateway实现)
   │ - 调用ZLMClient │
   └──────┬──────────┘
          │
          ▼
   ┌─────────────────┐
   │ ZLMClient       │ (Gateway封装)
   │ CloseRtpServer()│
   └──────┬──────────┘
          │ HTTP API调用
          ▼
   ┌─────────────────┐
   │ ZLMediaKit      │ (ZLM原生)
   │ - 关闭RTP服务器 │
   └─────────────────┘
```

### 3.3 为什么 GB28181 需要设备管理？

**原因**：GB28181 的设备发现和流管理是**紧密耦合**的

1. **设备发现是流管理的前提**：
   - 启动流前必须知道设备是否存在和在线
   - 需要获取设备IP用于发送SIP INVITE
   - 需要获取设备认证信息用于Digest认证

2. **设备状态影响流状态**：
   - 设备离线时，相关流应该停止
   - 设备重新上线时，可能需要恢复流

3. **与其他Gateway保持一致**：
   - 虽然GB28181是完整的系统标准，但在我们的架构中，它被归类为"设备发现Gateway"
   - 提供统一的设备管理接口，便于前端统一处理

## 4. 架构设计

### 4.1 整体架构（重新设计）

```
┌─────────────────────────────────────────────────────────────┐
│              GB28181 Gateway (我们实现)                      │
│                                                              │
│  ┌──────────────────────────────────────────────────────┐   │
│  │  SIP 服务器 (Gateway 实现)                           │   │
│  │  - UDP Socket 监听 (端口 5060)                       │   │
│  │  - SIP 消息解析 (使用 osip2/eXosip2)                 │   │
│  │  - REGISTER 处理 → 设备注册                           │   │
│  │  - MESSAGE 处理 → 设备心跳                            │   │
│  │  - INVITE 处理 → 视频点播请求                          │   │
│  │  - BYE 处理 → 停止推流                                │   │
│  │  - 事务管理和重传处理                                  │   │
│  └──────────────┬───────────────────────────────────────┘   │
│                 │                                            │
│  ┌──────────────▼───────────────────────────────────────┐   │
│  │  设备管理 (Gateway 实现)                              │   │
│  │  - 设备列表维护 (devices_)                            │   │
│  │  - 设备状态跟踪 (在线/离线)                            │   │
│  │  - 设备信息存储 (IP、认证信息等)                       │   │
│  │  - 心跳超时检测                                        │   │
│  └──────────────┬───────────────────────────────────────┘   │
│                 │                                            │
│  ┌──────────────▼───────────────────────────────────────┐   │
│  │  GB28181Gateway 核心逻辑 (Gateway 实现)                │   │
│  │  - Start(): 启动流                                    │   │
│  │    1. 验证设备存在和在线                               │   │
│  │    2. 调用 ZLMClient::OpenRtpServer()                 │   │
│  │    3. 发送 SIP INVITE                                 │   │
│  │  - Stop(): 停止流                                     │   │
│  │    1. 发送 SIP BYE                                    │   │
│  │    2. 调用 ZLMClient::CloseRtpServer()                │   │
│  │  - IsRunning(): 查询流状态                             │   │
│  │    调用 ZLMClient::GetStreamInfo()                    │   │
│  └──────────────┬───────────────────────────────────────┘   │
│                 │                                            │
│  ┌──────────────▼───────────────────────────────────────┐   │
│  │  ZLMClient (Gateway 封装 ZLM API)                     │   │
│  │  - OpenRtpServer() → ZLM API                          │   │
│  │  - CloseRtpServer() → ZLM API                         │   │
│  │  - ListRtpServer() → ZLM API                          │   │
│  │  - GetRtpInfo() → ZLM API                             │   │
│  │  - GetStreamInfo() → ZLM API                          │   │
│  └──────────────┬───────────────────────────────────────┘   │
└─────────────────┼──────────────────────────────────────────┘
                  │ HTTP API 调用
                  ▼
┌─────────────────────────────────────────────────────────────┐
│              ZLMediaKit (原生支持，直接使用)                 │
│                                                              │
│  ┌──────────────────────────────────────────────────────┐   │
│  │  RTP 服务器管理 (ZLM 原生)                            │   │
│  │  - openRtpServer API                                  │   │
│  │  - closeRtpServer API                                 │   │
│  │  - listRtpServer API                                  │   │
│  │  - getRtpInfo API                                     │   │
│  └──────────────┬───────────────────────────────────────┘   │
│                 │                                            │
│  ┌──────────────▼───────────────────────────────────────┐   │
│  │  RTP 流接收和处理 (ZLM 原生)                          │   │
│  │  - RTP over UDP/TCP 接收                              │   │
│  │  - PS 流解码 (GB28181Process)                         │   │
│  │  - H.264/H.265 视频解码                               │   │
│  │  - G.711/AAC 音频解码                                 │   │
│  │  - RTP 包排序和重组                                   │   │
│  │  - 流超时检测                                          │   │
│  └──────────────┬───────────────────────────────────────┘   │
│                 │                                            │
│  ┌──────────────▼───────────────────────────────────────┐   │
│  │  流格式转换和输出 (ZLM 原生)                           │   │
│  │  - HTTP-FLV                                           │   │
│  │  - HLS                                                │   │
│  │  - RTSP                                               │   │
│  │  - WebRTC                                             │   │
│  └───────────────────────────────────────────────────────┘   │
└─────────────────────────────────────────────────────────────┘
```

### 4.2 功能分工明确

#### Gateway 层负责（我们实现）

1. **SIP 服务器**
   - SIP 消息接收和解析
   - SIP 事务管理
   - REGISTER/INVITE/BYE/MESSAGE 处理
   - SDP 解析和构造

2. **设备管理**
   - 设备注册管理
   - 设备状态跟踪
   - 设备信息存储
   - 心跳超时检测

3. **流管理协调**
   - 启动流流程协调（验证设备 → 创建RTP服务器 → 发送INVITE）
   - 停止流流程协调（发送BYE → 关闭RTP服务器）
   - 流状态查询（调用ZLM API）

#### ZLM 层负责（原生支持，直接使用）

1. **RTP 服务器管理**
   - RTP 服务器创建/关闭
   - RTP 端口分配和管理
   - RTP 服务器列表查询

2. **RTP 流接收和处理**
   - RTP 包接收
   - PS 流解码
   - 音视频解码
   - 流超时检测

3. **流格式转换**
   - HTTP-FLV/HLS/RTSP/WebRTC 输出
   - 多路播放支持

### 4.3 数据流

#### 设备注册流程

```
GB28181设备 → SIP REGISTER → SIP服务器(Gateway) → 设备管理(Gateway)
                                                          ↓
                                                    添加到devices_
                                                          ↓
                                                   发送200 OK响应
```

#### 启动流流程

```
客户端API → GB28181Gateway::Start()
    ↓
1. 验证设备（设备管理）
    ↓
2. 创建RTP服务器（调用ZLMClient::OpenRtpServer）
    ↓
3. ZLM创建RTP服务器（ZLM原生功能）
    ↓
4. 获取RTP端口号
    ↓
5. 发送SIP INVITE（SIP服务器）
    ↓
6. 设备响应200 OK
    ↓
7. 设备推送RTP流 → ZLM接收和处理（ZLM原生功能）
    ↓
8. 流可在ZLM中播放（HTTP-FLV/HLS/RTSP/WebRTC）
```

#### 停止流流程

```
客户端API → GB28181Gateway::Stop()
    ↓
1. 发送SIP BYE（SIP服务器）
    ↓
2. 设备响应200 OK
    ↓
3. 关闭RTP服务器（调用ZLMClient::CloseRtpServer）
    ↓
4. ZLM关闭RTP服务器（ZLM原生功能）
    ↓
5. 从streams_映射中移除
```

### 3.2 工作流程

#### 设备注册流程

```
设备 → SIP服务器(内置) → Gateway设备列表
1. 设备发送 REGISTER 请求
2. SIP服务器处理注册
3. Gateway添加设备到列表
4. 发送 200 OK 响应
```

#### 视频点播流程

```
1. 通过API或SIP INVITE请求视频点播
2. Gateway创建RTP服务器
3. 获取分配的端口号
4. 通过SIP INVITE请求设备推流
5. 设备响应 200 OK（包含SDP）
6. 设备推送RTP流到ZLM
7. ZLM接收并处理RTP流
8. 流可在ZLM中播放
```

#### 停止推流流程

```
1. 通过API或SIP BYE请求停止推流
2. Gateway关闭RTP服务器
3. Gateway删除ZLM中的流
4. 从流列表中移除
```

---

## 5. 详细设计

### 4.1 ZLMClient 扩展

**文件**: `src/streaming/zlmediakit/zlm_client.hpp` / `.cpp`

**新增结构体**:
```cpp
struct RtpServerInfo {
    std::string stream_id;      // 流ID（设备ID或通道ID）
    int port = 0;               // RTP接收端口
    int tcp_mode = 0;           // 0=UDP, 1=TCP被动, 2=TCP主动
    bool enable_rtcp = false;   // 是否启用RTCP
    std::string local_ip;        // 本地IP地址
};
```

**新增方法**:
```cpp
// 创建GB28181 RTP服务器
RtpServerInfo OpenRtpServer(
    const std::string& stream_id,
    uint16_t port = 0,          // 0表示自动分配
    int tcp_mode = 0,
    bool enable_rtcp = false
);

// 关闭GB28181 RTP服务器
bool CloseRtpServer(const std::string& stream_id);

// 获取RTP服务器列表
std::vector<RtpServerInfo> ListRtpServer();

// 获取特定RTP服务器信息
RtpServerInfo GetRtpServerInfo(const std::string& stream_id);
```

**API 映射**:
- `OpenRtpServer` → `POST /index/api/openRtpServer`
- `CloseRtpServer` → `POST /index/api/closeRtpServer`
- `ListRtpServer` → `GET /index/api/listRtpServer`

### 4.2 GB28181Gateway 实现

**文件**: 
- `src/gateway/gb28181/gb28181_gateway.hpp`
- `src/gateway/gb28181/gb28181_gateway.cpp`

**设备管理的作用和价值**：

设备管理功能是GB28181Gateway的核心功能之一，具有以下重要作用：

1. **设备状态跟踪**
   - 记录哪些设备已注册（通过SIP REGISTER）
   - 跟踪设备在线/离线状态（通过心跳）
   - 记录设备最后心跳时间，自动检测离线设备

2. **设备信息存储**
   - 存储设备基本信息（ID、IP、端口、制造商、型号等）
   - 存储设备认证信息（用户名、密码，用于Digest认证）
   - 存储设备通道列表（多通道设备）

3. **启动流时的验证**
   - 启动流前检查设备是否存在
   - 检查设备是否在线（离线设备无法启动流）
   - 获取设备IP和认证信息，用于SIP INVITE

4. **前端展示和管理**
   - 前端设备发现页面展示所有GB28181设备
   - 用户可以查看设备状态（在线/离线）
   - 用户可以选择设备启动流
   - 显示设备基本信息（IP、制造商、型号等）

5. **设备认证管理**
   - 存储设备认证信息，用于SIP消息认证
   - 支持更新设备认证信息
   - 支持手动添加设备（用于测试或已知设备）

6. **与其他Gateway保持一致**
   - 提供统一的设备管理接口（DiscoverDevices、ListDevices、GetDevice等）
   - 前端可以统一处理不同协议的设备
   - 统一的设备卡片展示和操作

**实际使用场景**：

```
场景1: 设备自动注册
1. GB28181设备启动，发送SIP REGISTER
2. Gateway接收注册，添加到设备列表
3. 前端自动刷新，显示新设备

场景2: 启动设备流
1. 用户在设备卡片点击"启动流"
2. Gateway检查设备是否存在和在线
3. 获取设备IP和认证信息
4. 创建RTP服务器
5. 发送SIP INVITE请求设备推流

场景3: 设备离线检测
1. 设备停止发送心跳
2. Gateway检测到心跳超时
3. 更新设备状态为离线
4. 前端显示设备离线状态
5. 用户无法启动离线设备的流

场景4: 设备信息查询
1. 用户查看设备详情
2. 显示设备IP、通道列表、在线状态等
3. 可以更新设备认证信息
```

**类设计**:
```cpp
/**
 * @brief GB28181 设备信息
 */
struct GB28181Device {
    std::string id;                      // 设备ID（20位国标ID）
    std::string name;                     // 设备名称
    std::string manufacturer;             // 制造商
    std::string model;                    // 型号
    std::string ip;                       // 设备IP地址
    int port = 5060;                      // SIP端口
    std::string username;                 // 用户名（用于认证）
    std::string password;                 // 密码（用于认证）
    std::vector<std::string> channels;   // 通道列表
    std::chrono::system_clock::time_point last_seen;  // 最后心跳时间
    std::chrono::system_clock::time_point register_time;  // 注册时间
    bool online = false;                  // 是否在线
};

/**
 * @brief GB28181 Gateway
 */
class GB28181Gateway : public GatewayBase {
public:
    // GatewayBase 接口实现
    bool Start(const std::string& source_url,
              const std::string& target_app,
              const std::string& target_stream,
              const std::string& output_protocol = "") override;
    
    bool Stop(const std::string& target_app,
             const std::string& target_stream) override;
    
    bool IsRunning(const std::string& target_app,
                  const std::string& target_stream) override;
    
    GatewayStatus GetStatus(const std::string& target_app,
                           const std::string& target_stream) override;
    
    std::string GetProtocol() const override { return "gb28181"; }

    // 设备发现接口（与其他Gateway保持一致）
    /**
     * @brief 发现GB28181设备
     * 
     * 注意：GB28181设备通过SIP REGISTER主动注册，此方法主要用于：
     * 1. 查询已注册的设备列表
     * 2. 触发设备重新注册（如果支持）
     * 3. 前端展示设备列表
     * 
     * 实际设备发现是通过SIP REGISTER自动完成的
     */
    std::vector<GB28181Device> DiscoverDevices(int timeout_seconds = 5);
    
    /**
     * @brief 获取所有设备列表
     * 
     * 用途：
     * - 前端设备页面展示
     * - 设备状态查询
     * - 设备数量统计
     */
    std::vector<GB28181Device> ListDevices() const;
    
    /**
     * @brief 获取设备信息
     * 
     * 用途：
     * - 启动流前验证设备是否存在
     * - 获取设备IP和认证信息
     * - 前端设备详情页面展示
     */
    GB28181Device GetDevice(const std::string& device_id) const;
    
    /**
     * @brief 手动添加设备（用于测试或已知设备）
     * 
     * 用途：
     * - 测试环境手动添加设备
     * - 已知设备信息但未自动注册
     * - 设备认证信息配置
     */
    std::string AddDevice(const GB28181Device& device);
    
    /**
     * @brief 删除设备
     * 
     * 用途：
     * - 移除不再使用的设备
     * - 清理离线设备
     */
    bool RemoveDevice(const std::string& device_id);

    // GB28181 特定接口
    RtpServerInfo CreateRtpServer(...);
    RtpServerInfo GetRtpServerInfo(...) const;
    bool StartSipServer();
    void StopSipServer();

private:
    std::unique_ptr<SipServer> sip_server_;
    std::map<std::string, GB28181Device> devices_;
    std::map<std::string, StreamInfo> streams_;
    // ...
};
```

### 4.3 SIP服务器设计

**文件**: 
- `src/gateway/gb28181/sip_server.hpp`
- `src/gateway/gb28181/sip_server.cpp`

**关键实现细节**：

1. **SIP消息解析**：
   - 正确处理多行头部（以空格或TAB开头）
   - 处理URL编码
   - 解析SDP消息体

2. **事务管理**：
   - 维护Call-ID到事务的映射
   - 识别重传（相同Call-ID和CSeq）
   - 定期清理过期事务

3. **线程安全**：
   - 接收线程：快速接收消息，放入队列
   - 工作线程池：处理消息，避免阻塞
   - 关键操作（心跳响应）：快速处理

4. **错误处理**：
   - 所有异常都捕获并记录日志
   - 发送适当的SIP错误响应
   - 不因单个消息错误影响整体服务

### 4.4 配置扩展

**文件**: `src/config/config_loader.hpp`

**新增配置结构**:
```cpp
struct GB28181Config {
    bool enabled = true;
    
    // SIP服务器配置
    struct SipServerConfig {
        std::string local_ip = "0.0.0.0";
        int local_port = 5060;
        std::string server_id = "34020000002000000001";
        std::string domain = "3402000000";
        int register_expires = 3600;
        int heartbeat_interval = 60;
    } sip_server;
    
    // RTP服务器配置
    struct RtpServerConfig {
        uint16_t default_port = 0;
        std::string port_range = "30000-35000";
        int default_tcp_mode = 0;
        bool default_enable_rtcp = false;
    } rtp_server;
    
    // 设备管理配置
    struct DeviceConfig {
        int heartbeat_timeout = 300;
        bool auto_remove_offline = false;
    } device;
    
    // 流验证配置
    struct StreamValidationConfig {
        int check_interval_ms = 3000;
        int check_timeout_ms = 10000;
        int max_check_attempts = 10;
    } stream_validation;
    
    // 自动清理配置
    struct AutoCleanupConfig {
        bool enabled = true;
        int timeout_seconds = 300;
    } auto_cleanup;
};
```

### 4.5 HTTP API 扩展

**新增API端点**:

1. **发现GB28181设备**
   ```
   POST /api/v1/gb28181/devices/discover
   GET /api/v1/gb28181/devices
   GET /api/v1/gb28181/devices/{device_id}
   ```

2. **流管理**
   ```
   POST /api/v1/gb28181/streams
   DELETE /api/v1/gb28181/streams/{app}/{stream}
   GET /api/v1/gb28181/streams/{app}/{stream}
   GET /api/v1/gb28181/streams
   ```

详细API文档见第7节。

---

## 6. 部署注意事项

### 5.1 SIP 协议实现细节

#### SIP 消息解析

**关键点**：
- SIP 消息格式复杂，需要正确解析各种头部字段
- 必须支持 GB28181-2016 标准的扩展字段
- 需要处理多行头部（如 Via、Contact）

**实现建议**：
- 使用成熟的 SIP 解析库（推荐 osip2/eXosip2 或 pjsip）
- 如果必须自己实现，需要：
  1. 正确处理 CRLF 行结束符
  2. 处理多行头部（以空格或 TAB 开头）
  3. 处理 URL 编码
  4. 处理 SDP 解析（INVITE 消息中）

#### SIP 事务管理

**关键点**：
- SIP 使用事务（Transaction）机制，需要维护事务状态
- 必须正确处理重传（Retransmission）
- 需要维护 Call-ID 和 CSeq 的映射关系

**实现建议**：
```cpp
// 维护事务映射表
std::map<std::string, SipTransaction> transactions_;

// 处理重传：相同 CSeq 的请求视为重传
// 对于 INVITE：需要发送 100 Trying 防止重传
// 对于其他请求：直接重发上次响应
```

#### SDP 处理

**关键点**：
- INVITE 请求中包含 SDP（Session Description Protocol）
- 需要解析 SDP 获取设备的 RTP 接收地址
- 需要构建 SDP 响应，包含服务器的 RTP 地址

### 5.2 网络配置

#### NAT 穿透

**关键点**：
- 设备可能在 NAT 后面，需要处理 NAT 穿透
- RTP 流需要从设备发送到服务器，需要正确的 IP 地址

**实现建议**：
```cpp
// 检测本地IP地址（用于SDP中的c=行）
std::string GetLocalIP() {
    // 优先使用配置的IP
    if (!config->gb28181.sip_server.local_ip.empty() && 
        config->gb28181.sip_server.local_ip != "0.0.0.0") {
        return config->gb28181.sip_server.local_ip;
    }
    
    // 自动检测：获取与设备通信的网卡IP
    // 注意：不能使用 127.0.0.1，设备无法访问
}
```

#### 防火墙配置

**关键点**：
- SIP 端口（默认 5060/UDP）需要开放
- RTP 端口范围（30000-35000）需要开放
- 需要处理防火墙规则

#### 多网卡环境

**关键点**：
- 服务器可能有多个网卡，需要选择正确的网卡
- 不同设备可能在不同的网段

### 5.3 设备兼容性

#### GB28181 版本差异

**关键点**：
- GB28181-2011 和 GB28181-2016 有差异
- 不同厂商实现可能有差异

**实现建议**：
- 支持两种版本
- 根据设备能力自动选择
- 提供兼容性配置

#### 设备认证

**关键点**：
- 某些设备需要认证（Digest Authentication）
- 需要正确处理 WWW-Authenticate 和 Authorization 头部

#### 设备心跳

**关键点**：
- 设备定期发送 MESSAGE 心跳
- 需要正确响应，否则设备会认为服务器离线

### 5.4 错误处理和容错

#### SIP 消息错误处理

**关键点**：
- 需要处理各种 SIP 错误（400 Bad Request、401 Unauthorized 等）
- 需要记录错误日志，便于调试

#### RTP 服务器创建失败

**关键点**：
- 端口可能被占用
- ZLM API 可能失败

**实现建议**：
- 失败后重试（使用不同端口）
- 记录详细错误信息

#### 流超时处理

**关键点**：
- 设备可能停止推流，需要检测并清理
- 避免资源泄漏

### 5.5 性能考虑

#### SIP 消息处理性能

**关键点**：
- SIP 消息处理需要快速，避免阻塞
- 使用异步处理或线程池

#### 内存管理

**关键点**：
- 避免内存泄漏
- 及时释放不用的资源

**实现建议**：
- 使用智能指针
- 定期清理过期事务
- 使用 RAII 管理资源

### 5.6 日志和调试

#### 详细日志

**关键点**：
- 需要详细的日志便于调试
- 区分不同级别的日志

#### SIP 消息转储

**关键点**：
- 可以转储 SIP 消息到文件，便于分析

### 5.7 配置验证

#### 启动时配置检查

**关键点**：
- 启动时验证配置是否正确
- 检查端口是否可用

**检查项**：
- SIP 端口可用性
- 服务器ID格式（20位数字）
- 域格式（10位数字）
- ZLM 连接状态

### 5.8 依赖库

#### SIP 库选择

**推荐方案**：

1. **osip2/eXosip2** (推荐)
   - 优点：轻量级，C语言，易于集成
   - 缺点：需要自己处理一些细节
   - 许可证：LGPL

2. **pjsip**
   - 优点：功能完整，支持多种协议
   - 缺点：体积较大，配置复杂
   - 许可证：GPL/商业许可

**安装**：
```bash
# Ubuntu/Debian
sudo apt-get install libosip2-dev libeXosip2-dev

# macOS
brew install libosip

# 或从源码编译
./scripts/setup/install_sip_libs.sh
```

### 5.9 线程安全

#### 设备列表线程安全

**关键点**：
- 设备列表可能被多个线程访问
- 需要正确的锁机制

**实现建议**：
- 使用读写锁（如果读多写少）
- 避免死锁（锁的顺序要一致）

#### SIP 消息处理线程安全

**关键点**：
- SIP 消息可能并发到达
- 需要线程安全的消息队列

### 5.10 部署检查清单

#### 部署前检查

- [ ] ZLM 服务已启动并正常运行
- [ ] SIP 端口（5060）未被占用
- [ ] RTP 端口范围（30000-35000）未被占用
- [ ] 防火墙已配置，开放必要端口
- [ ] 网络配置正确（IP地址、网卡选择）
- [ ] 配置文件正确（服务器ID、域等）
- [ ] 依赖库已安装（osip2/eXosip2）

#### 部署后验证

- [ ] SIP 服务器成功启动
- [ ] 可以接收设备注册
- [ ] 设备心跳正常
- [ ] 可以创建 RTP 服务器
- [ ] 设备可以推送 RTP 流
- [ ] 流可以在 ZLM 中播放
- [ ] 日志正常，无错误

#### 常见问题排查

1. **设备无法注册**
   - 检查 SIP 端口是否开放
   - 检查服务器ID和域配置
   - 查看 SIP 消息日志

2. **RTP 流无法接收**
   - 检查 RTP 端口是否开放
   - 检查 IP 地址配置（不能是 127.0.0.1）
   - 查看 ZLM 日志

3. **设备频繁离线**
   - 检查心跳响应是否及时
   - 检查网络连接
   - 查看心跳超时配置

---

## 7. 实现步骤（重新设计）

### 核心原则

**充分利用 ZLM 原生功能，只实现 ZLM 不支持的部分**

#### ZLM 已支持（直接调用 API，无需实现）
- ✅ RTP 服务器创建/关闭/列表查询
- ✅ RTP 流接收和处理（PS流、H.264/H.265解码）
- ✅ 流状态查询和监控
- ✅ 流格式转换（HTTP-FLV/HLS/RTSP/WebRTC）
- ✅ 流超时检测和自动清理

#### Gateway 需要实现（ZLM 不支持）
- ❌ SIP 服务器（REGISTER/INVITE/BYE/MESSAGE）
- ❌ 设备管理（设备列表、状态跟踪）
- ❌ SIP 信令处理（SDP解析、事务管理）
- ❌ 设备认证（Digest Authentication）

---

### 阶段1: ZLMClient 扩展（优先级：高）

**目标**：封装 ZLM 的 RTP 服务器 API，提供易用的 C++ 接口

**任务清单**：
1. ✅ 在 `ZLMClient` 中添加 `OpenRtpServer` 方法
   - 调用 ZLM API: `POST /index/api/openRtpServer`
   - 参数：`port`, `tcp_mode`, `stream_id`, `local_ip`, `ssrc`, `only_track`
   - 返回：`RtpServerInfo`（包含分配的端口号）

2. ✅ 在 `ZLMClient` 中添加 `CloseRtpServer` 方法
   - 调用 ZLM API: `POST /index/api/closeRtpServer`
   - 参数：`stream_id`, `app`, `vhost`
   - 返回：是否成功

3. ✅ 在 `ZLMClient` 中添加 `ListRtpServer` 方法
   - 调用 ZLM API: `GET /index/api/listRtpServer`
   - 返回：`std::vector<RtpServerInfo>`

4. ✅ 在 `ZLMClient` 中添加 `GetRtpInfo` 方法
   - 调用 ZLM API: `GET /index/api/getRtpInfo`
   - 参数：`stream_id`, `app`, `vhost`
   - 返回：RTP 连接信息（peer_ip, peer_port, local_ip, local_port）

5. ✅ 在 `ZLMClient` 中添加 `UpdateRtpServerSSRC` 方法（可选）
   - 调用 ZLM API: `POST /index/api/updateRtpServerSSRC`
   - 用于更新 SSRC（某些设备需要）

**实现要点**：
- 直接调用 ZLM HTTP API，不重复实现 RTP 服务器逻辑
- 错误处理和日志记录
- 参数验证和默认值处理

**预计工作量**：1-2 天

---

### 阶段2: SIP 服务器实现（优先级：高）

**目标**：实现完整的 SIP 服务器，处理设备注册、心跳、控制

**任务清单**：
1. ✅ 创建 `SipServer` 类框架
   - UDP Socket 监听（端口 5060）
   - 消息接收和解析
   - 消息路由和分发

2. ✅ 实现 SIP 消息解析
   - 使用 osip2/eXosip2 库（推荐）或自己实现
   - 解析 REGISTER/INVITE/BYE/MESSAGE
   - 解析 SDP（INVITE 消息中）

3. ✅ 实现 REGISTER 处理
   - 接收设备注册请求
   - 提取设备信息（ID、IP、端口）
   - 添加到设备列表
   - 发送 200 OK 响应

4. ✅ 实现 MESSAGE 处理（心跳）
   - 接收心跳消息
   - 更新设备 `last_seen` 时间
   - 发送 200 OK 响应

5. ✅ 实现事务管理
   - 维护 Call-ID 到事务的映射
   - 处理重传（相同 Call-ID 和 CSeq）
   - 定期清理过期事务

6. ✅ 实现错误处理
   - 发送适当的 SIP 错误响应（400/401/500等）
   - 记录详细日志
   - 异常处理

**实现要点**：
- 使用成熟的 SIP 库（osip2/eXosip2），避免重复实现 SIP 协议细节
- 线程安全（接收线程 + 工作线程池）
- 快速响应心跳（避免阻塞）

**预计工作量**：5-7 天

---

### 阶段3: GB28181Gateway 核心功能（优先级：高）

**目标**：实现 Gateway 核心功能，整合 SIP 服务器和 ZLM RTP 服务器

**任务清单**：
1. ✅ 创建 `GB28181Gateway` 类框架
   - 继承 `GatewayBase`
   - 实现 `Start/Stop/IsRunning/GetStatus` 方法
   - 集成 `SipServer` 和 `ZLMClient`

2. ✅ 实现设备管理
   - `DiscoverDevices()`: 查询已注册设备（实际是查询设备列表）
   - `ListDevices()`: 获取所有设备
   - `GetDevice()`: 获取设备信息
   - `AddDevice()`: 手动添加设备（测试用）
   - `RemoveDevice()`: 删除设备
   - 设备状态跟踪（在线/离线）

3. ✅ 实现 `Start` 方法（启动流）
   - 解析 `source_url`（格式：`gb28181://device_id/channel_id`）
   - 验证设备是否存在和在线
   - 调用 `ZLMClient::OpenRtpServer` 创建 RTP 服务器
   - 获取分配的端口号
   - 发送 SIP INVITE 请求设备推流
   - 等待设备响应 200 OK
   - 记录流信息到 `streams_` 映射

4. ✅ 实现 `Stop` 方法（停止流）
   - 发送 SIP BYE 请求设备停止推流
   - 调用 `ZLMClient::CloseRtpServer` 关闭 RTP 服务器
   - 从 `streams_` 映射中移除

5. ✅ 实现 `IsRunning` 和 `GetStatus` 方法
   - 调用 `ZLMClient::GetStreamInfo` 查询流状态
   - 结合设备状态和流状态判断

6. ✅ 实现 INVITE 处理（SIP 服务器回调）
   - 解析 INVITE 请求（包含 SDP）
   - 构造 SDP 响应（包含 RTP 服务器地址）
   - 发送 200 OK 响应

7. ✅ 实现 BYE 处理（SIP 服务器回调）
   - 接收设备发来的 BYE
   - 关闭对应的 RTP 服务器
   - 发送 200 OK 响应

**实现要点**：
- **充分利用 ZLM 功能**：RTP 服务器创建/关闭直接调用 ZLM API
- **流状态查询**：直接使用 `ZLMClient::GetStreamInfo`，不重复实现
- **设备状态和流状态结合**：设备离线时，流也应该停止

**预计工作量**：7-10 天

---

### 阶段4: 配置和集成（优先级：中）

**任务清单**：
1. ✅ 扩展配置结构
   - 在 `config_loader.hpp` 中添加 `GB28181Config`
   - 解析配置文件中的 GB28181 配置

2. ✅ 集成到 GatewayFactory
   - 在 `GatewayFactory` 中创建 `GB28181Gateway` 实例
   - 根据配置决定是否启用

3. ✅ 添加 HTTP API 端点
   - 设备管理 API：`/api/v1/gb28181/devices/*`
   - 流管理 API：`/api/v1/gb28181/streams/*`
   - 实现 API Handler

4. ✅ 更新 main.cpp
   - 传递 `GB28181Gateway` 到 `HttpServer`

**预计工作量**：2-3 天

---

### 阶段5: 完善功能（优先级：中）

**任务清单**：
1. ✅ 实现设备认证（Digest Authentication）
   - 处理 WWW-Authenticate 头部
   - 验证 Authorization 头部
   - 支持设备认证信息配置

2. ✅ 实现设备心跳超时检测
   - 定期检查设备 `last_seen` 时间
   - 自动标记离线设备
   - 可选：自动清理离线设备的流

3. ✅ 实现流状态验证
   - 定期检查流是否建立（调用 `ZLMClient::GetStreamInfo`）
   - 如果流未建立，重试或标记失败

4. ✅ 优化性能
   - 线程池处理 SIP 消息
   - 批量更新设备状态
   - 内存优化（及时清理过期数据）

5. ✅ 错误处理和容错
   - RTP 服务器创建失败重试
   - SIP 消息错误处理
   - 网络异常恢复

**预计工作量**：5-7 天

---

### 阶段6: 测试和文档（优先级：高）

**任务清单**：
1. ✅ 单元测试
   - ZLMClient RTP 服务器 API 测试
   - SIP 消息解析测试
   - 设备管理测试

2. ✅ 集成测试
   - 使用 GB28181 设备模拟器测试完整流程
   - 测试设备注册 → 启动流 → 播放 → 停止流

3. ✅ 压力测试
   - 多设备并发注册
   - 多流并发启动
   - 长时间运行稳定性测试

4. ✅ 文档更新
   - API 文档
   - 部署文档
   - 测试文档

**预计工作量**：5-7 天

---

### 总体时间估算

| 阶段 | 工作量 | 优先级 |
|------|--------|--------|
| 阶段1: ZLMClient 扩展 | 1-2 天 | 高 |
| 阶段2: SIP 服务器 | 5-7 天 | 高 |
| 阶段3: GB28181Gateway 核心 | 7-10 天 | 高 |
| 阶段4: 配置和集成 | 2-3 天 | 中 |
| 阶段5: 完善功能 | 5-7 天 | 中 |
| 阶段6: 测试和文档 | 5-7 天 | 高 |
| **总计** | **25-36 天** | |

---

### 关键设计决策

#### 1. 充分利用 ZLM 原生功能

**原则**：ZLM 已经实现的功能，我们只封装 API，不重复实现

**示例**：
```cpp
// ✅ 正确：直接调用 ZLM API
RtpServerInfo GB28181Gateway::CreateRtpServer(...) {
    return zlm_client_->OpenRtpServer(stream_id, port, tcp_mode);
}

// ❌ 错误：不要自己实现 RTP 服务器
// 不要自己创建 UDP Socket、处理 RTP 包等
```

#### 2. Gateway 只实现 ZLM 不支持的功能

**原则**：Gateway 层只实现 SIP 服务器、设备管理、SIP 信令处理

**示例**：
```cpp
// ✅ Gateway 实现：SIP 服务器
class SipServer {
    void HandleRegister(const SipMessage& msg);
    void HandleInvite(const SipMessage& msg);
    // ...
};

// ✅ Gateway 实现：设备管理
class GB28181Gateway {
    std::map<std::string, GB28181Device> devices_;
    // ...
};

// ❌ Gateway 不实现：RTP 流接收和处理（ZLM 已支持）
```

#### 3. 流状态查询直接使用 ZLM API

**原则**：流状态查询直接调用 `ZLMClient::GetStreamInfo`，不维护自己的流状态

**示例**：
```cpp
// ✅ 正确：直接查询 ZLM
bool GB28181Gateway::IsRunning(...) {
    auto stream_info = zlm_client_->GetStreamInfo(app, stream);
    return ZLMClient::IsStreamActive(stream_info);
}

// ❌ 错误：不要自己维护流状态
// 不要自己跟踪 RTP 包、判断流是否活跃等
```

#### 4. 设备状态和流状态结合

**原则**：设备离线时，相关流也应该停止

**示例**：
```cpp
bool GB28181Gateway::Start(...) {
    // 1. 检查设备状态
    auto device = GetDevice(device_id);
    if (!device.online) {
        return false;  // 设备离线，无法启动流
    }
    
    // 2. 创建 RTP 服务器（调用 ZLM API）
    auto rtp_info = zlm_client_->OpenRtpServer(...);
    
    // 3. 发送 SIP INVITE（Gateway 实现）
    SendInvite(device, rtp_info);
    
    // 4. 流状态由 ZLM 管理，我们只记录流信息
    streams_[stream_id] = StreamInfo{...};
}
```

---

### 开发优先级建议

**MVP（最小可行产品）**：
1. 阶段1: ZLMClient 扩展（必须）
2. 阶段2: SIP 服务器基础功能（REGISTER + MESSAGE）（必须）
3. 阶段3: GB28181Gateway 核心功能（Start/Stop）（必须）
4. 阶段4: 配置和集成（必须）

**完整功能**：
5. 阶段2: SIP 服务器完整功能（INVITE + BYE）
6. 阶段5: 完善功能（认证、心跳检测等）
7. 阶段6: 测试和文档

**建议**：
- 先完成 MVP，确保基本可用
- 再逐步完善功能
- 每个阶段完成后进行测试

---

## 8. API 使用示例

### 7.1 HTTP API

#### 发现设备

```bash
# 发现设备（设备会自动通过SIP REGISTER注册，此API用于查询）
curl -X POST "http://localhost:8080/api/v1/gb28181/devices/discover" \
  -H "Content-Type: application/json" \
  -d '{"timeout_seconds": 5}'

# 列出所有设备
curl "http://localhost:8080/api/v1/gb28181/devices"

# 获取设备信息
curl "http://localhost:8080/api/v1/gb28181/devices/34020000001320000001"
```

#### 流管理

```bash
# 启动设备流
curl -X POST "http://localhost:8080/api/v1/gb28181/streams" \
  -H "Content-Type: application/json" \
  -d '{
    "source_url": "gb28181://34020000001320000001/1",
    "target_app": "rtp",
    "target_stream": "34020000001320000001_1"
  }'

# 停止流
curl -X DELETE "http://localhost:8080/api/v1/gb28181/streams/rtp/34020000001320000001_1"

# 获取流信息
curl "http://localhost:8080/api/v1/gb28181/streams/rtp/34020000001320000001_1"

# 列出所有流
curl "http://localhost:8080/api/v1/gb28181/streams"
```

### 7.2 C++ API

```cpp
// 获取GB28181Gateway
auto gb28181_gateway = gateway_factory.GetGB28181Gateway();

// 发现设备
auto devices = gb28181_gateway->DiscoverDevices(5);

// 启动流
bool success = gb28181_gateway->Start(
    "gb28181://34020000001320000001/1",  // source_url
    "rtp",                                // target_app
    "34020000001320000001_1"             // target_stream
);

// 获取RTP服务器信息
auto rtp_info = gb28181_gateway->GetRtpServerInfo("rtp", "34020000001320000001_1");
std::cout << "RTP Port: " << rtp_info.port << std::endl;

// 停止流
gb28181_gateway->Stop("rtp", "34020000001320000001_1");
```

### 7.3 Python 示例

```python
import requests

# 1. 发现设备
response = requests.post("http://gateway:8080/api/v1/gb28181/devices/discover", json={
    "timeout_seconds": 5
})
devices = response.json()["data"]

# 2. 启动设备流
response = requests.post("http://gateway:8080/api/v1/gb28181/streams", json={
    "source_url": "gb28181://34020000001320000001/1",
    "target_app": "rtp",
    "target_stream": "34020000001320000001_1"
})
result = response.json()
rtp_port = result["data"]["rtp_server"]["port"]
play_url = result["data"]["play_urls"]["http_flv"]

# 3. 停止流
requests.delete("http://gateway:8080/api/v1/gb28181/streams/rtp/34020000001320000001_1")
```

---

## 9. 设备管理功能详解

### 8.1 为什么需要设备管理？

设备管理是GB28181Gateway的核心功能，具有以下重要作用：

#### 8.1.1 设备状态跟踪

**问题**：如何知道哪些设备在线？哪些设备离线？

**解决方案**：设备管理维护设备列表，跟踪设备状态
- 设备通过SIP REGISTER注册时，添加到设备列表，状态设为在线
- 设备发送心跳时，更新`last_seen`时间
- 定期检查心跳超时，自动标记离线设备

**实际应用**：
```cpp
// 启动流前检查设备状态
GB28181Device device = GetDevice(device_id);
if (device.id.empty()) {
    return false;  // 设备不存在
}
if (!device.online) {
    return false;  // 设备离线，无法启动流
}
```

#### 8.1.2 设备信息存储

**问题**：启动流时需要设备IP和认证信息，从哪里获取？

**解决方案**：设备管理存储设备信息
- 设备注册时，从SIP REGISTER消息中提取设备信息（IP、端口等）
- 存储设备认证信息（用户名、密码）
- 存储设备通道列表

**实际应用**：
```cpp
// 启动流时获取设备信息
GB28181Device device = GetDevice(device_id);
std::string device_ip = device.ip;  // 用于SIP INVITE
std::string username = device.username;  // 用于Digest认证
std::string password = device.password;
```

#### 8.1.3 前端展示和管理

**问题**：用户如何查看和管理设备？

**解决方案**：设备管理提供统一的API接口
- `ListDevices()`: 获取所有设备列表，前端展示
- `GetDevice()`: 获取设备详情，前端设备卡片展示
- `DiscoverDevices()`: 触发设备发现，前端设备发现页面

**实际应用**：
```typescript
// 前端设备页面
const devices = await api.get('/api/v1/gb28181/devices');
// 展示设备列表，每个设备显示：
// - 设备ID、名称
// - 在线状态（在线/离线）
// - IP地址
// - 通道列表
// - 启动流按钮
```

#### 8.1.4 与其他Gateway保持一致

**问题**：为什么GB28181也需要设备管理？

**解决方案**：统一接口，便于前端统一处理
- 所有设备发现Gateway（ONVIF、ISAPI、GB28181等）都提供相同的设备管理接口
- 前端可以统一展示和管理不同协议的设备
- 统一的设备卡片组件，统一的启动流流程

**实际应用**：
```typescript
// 前端统一处理不同协议的设备
const devices = [
  ...onvifDevices,    // ONVIF设备
  ...isapiDevices,    // ISAPI设备
  ...gb28181Devices,  // GB28181设备
];

// 统一的设备卡片展示
devices.map(device => <DeviceCard device={device} />);
```

### 8.2 设备管理的工作流程

#### 8.2.1 设备注册流程

```
1. GB28181设备启动
   ↓
2. 设备发送SIP REGISTER到Gateway
   ↓
3. SIP服务器接收REGISTER消息
   ↓
4. Gateway解析设备信息：
   - 设备ID（From头部）
   - 设备IP（Contact头部或Via头部）
   - 设备端口
   ↓
5. 添加到设备列表（devices_）：
   - device.id = "34020000001320000001"
   - device.ip = "192.168.1.100"
   - device.online = true
   - device.register_time = now
   ↓
6. 发送200 OK响应
   ↓
7. 前端通过WebSocket或轮询获取设备更新
   ↓
8. 前端设备页面显示新设备
```

#### 8.2.2 设备心跳流程

```
1. 设备定期发送SIP MESSAGE（心跳）
   ↓
2. Gateway接收心跳消息
   ↓
3. 更新设备信息：
   - device.last_seen = now
   - device.online = true（如果之前是离线）
   ↓
4. 发送200 OK响应
   ↓
5. 定期检查心跳超时：
   - 如果 now - device.last_seen > heartbeat_timeout
   - 设置 device.online = false
```

#### 8.2.3 启动流时的设备验证

```
1. 用户点击"启动流"按钮
   ↓
2. 前端调用API: POST /api/v1/gb28181/streams
   {
     "source_url": "gb28181://34020000001320000001/1"
   }
   ↓
3. Gateway解析source_url，提取device_id和channel_id
   ↓
4. **设备管理验证**：
   - 调用 GetDevice(device_id)
   - 检查设备是否存在
   - 检查设备是否在线
   ↓
5. 如果设备不存在或离线，返回错误
   ↓
6. 如果设备在线，获取设备信息：
   - device.ip → 用于SIP INVITE
   - device.username/password → 用于认证
   ↓
7. 创建RTP服务器
   ↓
8. 发送SIP INVITE（使用设备IP和认证信息）
   ↓
9. 设备响应200 OK，开始推流
```

### 8.3 设备管理 vs 流管理

**设备管理**（Device Management）：
- **作用**：管理设备本身（设备信息、状态、认证等）
- **生命周期**：设备注册 → 设备在线 → 设备离线
- **数据**：设备列表（devices_）
- **用途**：设备发现、状态跟踪、信息存储

**流管理**（Stream Management）：
- **作用**：管理设备的流（RTP流、流状态等）
- **生命周期**：流启动 → 流运行 → 流停止
- **数据**：流列表（streams_）
- **用途**：流状态跟踪、RTP服务器管理

**关系**：
- 一个设备可以有多个流（多通道设备）
- 启动流前必须先有设备（设备管理）
- 设备离线时，相关流也应该停止（流管理）

### 8.4 设备管理的实际价值

1. **用户体验**：
   - 前端可以展示所有设备，用户一目了然
   - 显示设备在线状态，用户知道哪些设备可用
   - 点击设备卡片即可启动流，无需手动输入设备ID

2. **系统可靠性**：
   - 启动流前验证设备存在性和在线状态
   - 避免对离线设备启动流（节省资源）
   - 自动检测设备离线，及时清理相关流

3. **系统可维护性**：
   - 统一的设备管理接口，便于扩展
   - 设备信息集中管理，便于查询和调试
   - 支持设备认证信息管理

4. **与其他Gateway统一**：
   - 前端可以统一处理不同协议的设备
   - 统一的设备卡片和操作界面
   - 降低前端开发复杂度

## 10. 常见问题

### 9.1 端口被占用

**问题**：创建 RTP 服务器时提示端口被占用

**解决**：
- 检查端口是否被其他程序占用：`lsof -i :PORT`
- 扩大 `port_range` 范围
- 手动指定可用端口

### 9.2 流接收超时

**问题**：流创建后很快超时消失

**解决**：
- 检查设备是否正确推流
- 检查网络连接和防火墙
- 增加 `timeoutSec` 值
- 检查 `udp_recv_socket_buffer` 是否足够大

### 9.3 音视频不同步

**问题**：播放时音视频不同步

**解决**：
- 检查设备推流的音视频时间戳
- 调整 `rtp_g711_dur_ms` 参数
- 检查网络延迟和丢包情况

### 9.4 无法播放流

**问题**：流已接收但无法播放

**解决**：
- 检查流的编码格式（H264/H265）
- 查看 ZLM 日志确认流是否正常
- 使用 `getMediaInfo` API 检查流信息
- 检查协议是否启用（RTSP/HTTP-FLV/HLS）

### 9.5 设备无法注册

**问题**：设备无法通过SIP REGISTER注册

**解决**：
- 检查 SIP 端口是否开放
- 检查服务器ID和域配置
- 查看 SIP 消息日志
- 检查设备配置（服务器地址、端口等）

### 9.6 RTP 流无法接收

**问题**：设备推送RTP流但无法接收

**解决**：
- 检查 RTP 端口是否开放
- 检查 IP 地址配置（不能是 127.0.0.1）
- 查看 ZLM 日志
- 检查防火墙规则

### 9.7 设备频繁离线

**问题**：设备频繁显示离线

**解决**：
- 检查心跳响应是否及时
- 检查网络连接
- 查看心跳超时配置
- 检查设备心跳间隔设置

---

### 9.8 设备管理相关问题

#### 问题1：设备已注册但无法启动流

**可能原因**：
- 设备状态为离线（心跳超时）
- 设备信息不完整（缺少IP或认证信息）

**解决方法**：
```bash
# 1. 检查设备状态
curl "http://localhost:8080/api/v1/gb28181/devices/34020000001320000001"

# 2. 查看设备是否在线
# 如果 online: false，检查心跳配置

# 3. 手动更新设备信息（如果需要）
curl -X PUT "http://localhost:8080/api/v1/gb28181/devices/34020000001320000001" \
  -H "Content-Type: application/json" \
  -d '{
    "ip": "192.168.1.100",
    "username": "admin",
    "password": "password"
  }'
```

#### 问题2：设备列表为空

**可能原因**：
- 设备未注册（SIP REGISTER未收到）
- 设备注册失败（认证失败等）

**解决方法**：
```bash
# 1. 检查SIP服务器是否运行
curl "http://localhost:8080/api/v1/status"

# 2. 查看SIP日志
tail -f logs/gateway.log | grep -i "register\|gb28181"

# 3. 检查设备配置（设备端）
# 确保设备配置了正确的SIP服务器地址和端口
```

#### 问题3：设备频繁上线/下线

**可能原因**：
- 心跳响应慢
- 心跳超时配置过短
- 网络不稳定

**解决方法**：
```json
// 调整配置
{
  "gb28181": {
    "device": {
      "heartbeat_timeout": 600  // 增加到10分钟
    }
  }
}
```

## 11. 参考资源

### 9.1 官方文档

- [ZLMediaKit 官方文档](https://github.com/ZLMediaKit/ZLMediaKit)
- [GB28181 标准文档](https://www.gb688.cn/)

### 9.2 相关项目

- [wvp-GB28181-pro](https://github.com/648540858/wvp-GB28181-pro) - Java实现的GB28181平台
- [AKStream](https://github.com/chatop2020/AKStream) - C#实现的GB28181平台
- [BXC_SipServer](https://github.com/any12345com/BXC_SipServer) - C++实现的GB28181服务器
- [gosip](https://github.com/panjjo/gosip) - Go实现的GB28181服务器

### 9.3 SIP 库

- [osip2/eXosip2](https://www.gnu.org/software/osip/) - 轻量级SIP库
- [pjsip](https://www.pjsip.org/) - 完整的SIP栈

### 9.4 调试工具

- [Wireshark](https://www.wireshark.org/) - 网络抓包工具
- [SIPp](https://github.com/SIPp/sipp) - SIP测试工具

---

## 附录

### A. 文件清单

#### 新增文件
- `src/gateway/gb28181/gb28181_gateway.hpp`
- `src/gateway/gb28181/gb28181_gateway.cpp`
- `src/gateway/gb28181/sip_server.hpp`
- `src/gateway/gb28181/sip_server.cpp`
- `src/gateway/gb28181/sip_message.hpp`
- `src/gateway/gb28181/sip_message.cpp`
- `src/gateway/gb28181/sdp_parser.hpp`
- `src/gateway/gb28181/sdp_parser.cpp`
- `src/gateway/gb28181/thread_safe_queue.hpp`

#### 修改文件
- `src/streaming/zlmediakit/zlm_client.hpp` - 添加RTP服务器API
- `src/streaming/zlmediakit/zlm_client.cpp` - 实现RTP服务器API
- `src/config/config_loader.hpp` - 添加GB28181配置
- `src/config/config_loader.cpp` - 解析GB28181配置
- `src/gateway/utils/gateway_factory.hpp` - 添加GB28181Gateway
- `src/gateway/utils/gateway_factory.cpp` - 创建GB28181Gateway
- `src/api/http_server.hpp` - 添加GB28181 API端点声明
- `src/api/http_server.cpp` - 实现GB28181 API端点
- `src/main.cpp` - 传递GB28181Gateway到HttpServer
- `configs/config.example.json` - 添加GB28181配置示例

### B. 配置示例

完整配置示例见第2.2节。

### C. 性能优化建议

1. **消息处理优化**
   - 使用线程池处理SIP消息
   - 关键操作（心跳）快速响应
   - 批量处理设备状态更新

2. **内存优化**
   - 及时清理过期事务
   - 限制设备列表大小
   - 使用对象池复用对象

3. **网络优化**
   - 使用UDP多播（如果支持）
   - 优化SDP大小
   - 减少不必要的SIP消息

### D. 安全考虑

1. **认证**
   - 支持设备认证（Digest Authentication）
   - 验证设备ID格式
   - 限制注册频率

2. **防攻击**
   - 限制消息大小
   - 限制连接数
   - 防止DoS攻击

3. **日志安全**
   - 不记录敏感信息（密码等）
   - 日志文件权限控制
   - 定期清理日志

---

**文档版本**: 1.0  
**最后更新**: 2024-01-01  
**维护者**: ZLM Gateway Team

