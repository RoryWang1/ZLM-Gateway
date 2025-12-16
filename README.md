# ZLMediaKit Gateway - C++实现

## 项目概述

这是MediaMTX Gateway的C++实现版本，使用ZLMediaKit作为流媒体服务器，统一使用C/C++技术栈。

## 技术栈

- **语言**: C++17
- **流媒体服务器**: ZLMediaKit (C++)
- **协议转换**: FFmpeg (libav)
- **HTTP服务器**: httplib
- **HTTP客户端**: libcurl
- **JSON处理**: nlohmann/json
- **日志**: spdlog
- **构建系统**: Makefile

## 项目结构

```
ZLM-gateway/
├── src/                    # 源代码
│   ├── gateway/           # Gateway核心
│   │   ├── base/         # 基础类
│   │   ├── rtsp/         # RTSP Gateway
│   │   ├── rtmp/         # RTMP Gateway
│   │   ├── onvif/        # ONVIF Gateway
│   │   ├── isapi/        # ISAPI Gateway
│   │   ├── dahua/        # 大华Gateway
│   │   ├── psia/         # PSIA Gateway
│   │   ├── quic/         # QUIC Gateway
│   │   ├── dash/         # DASH Gateway
│   │   └── ndi/          # NDI Gateway
│   ├── streaming/         # 流媒体服务器管理
│   │   ├── zlmediakit/   # ZLMediaKit集成

│   ├── config/            # 配置管理
│   ├── api/               # HTTP API服务器
│   ├── utils/             # 工具类
│   └── process/           # 进程管理
├── include/               # 头文件
├── third_party/           # 第三方库
├── configs/               # 配置文件
├── scripts/               # 构建脚本
├── tests/                 # 测试
├── bin/                   # 编译输出
├── build/                 # 构建文件
└── Makefile               # Makefile构建文件
```

## 快速开始

### 1. 安装依赖

**注意**: 项目已包含 FFmpeg 预编译二进制文件（位于 `third_party/ffmpeg/`），无需单独下载。但是对于 Linux ARM64 (Ubuntu)，推荐使用系统源安装。

**Ubuntu/Debian (推荐 ARM64 使用):**
```bash
make deps-ubuntu
# 或手动安装
sudo apt-get update
sudo apt-get install -y \
    build-essential \
    g++ \
    pkg-config \
    ffmpeg \
    libavformat-dev \
    libavcodec-dev \
    libavutil-dev \
    libavfilter-dev \
    libswscale-dev \
    libcurl4-openssl-dev

```

**macOS:**
```bash
make deps-macos
# 或手动安装（FFmpeg 开发库，用于编译时链接）
brew install cmake pkg-config ffmpeg curl

```

**说明**:
- **编译时**: 需要系统安装 FFmpeg 开发库（libavformat-dev 等）用于链接
- **运行时**: 使用项目内的 FFmpeg 二进制文件（`third_party/ffmpeg/`）

### 2. 检查依赖

```bash
make check-deps
```

### 3. 构建项目

```bash
# Release版本（默认）
make

# 或Debug版本
make debug

# 或指定Release
make release
```

### 4. 运行

```bash
# 启动Gateway Manager
./bin/gateway_manager

# 或启动单个Gateway
./bin/rtsp_gateway
./bin/rtmp_gateway
./bin/onvif_gateway
```

### 5. 清理

```bash
make clean
```

## 配置

### ZLM (ZLMediaKit) 配置

**重要**: 在启动 Gateway 之前，需要先配置并启动 ZLMediaKit。

#### 1. 验证配置

运行配置验证脚本检查配置是否正确：

```bash
./scripts/verify_zlm_config.sh
```

#### 2. 启动 ZLMediaKit

```bash
# 使用启动脚本（推荐）
./scripts/setup/start_zlmediakit.sh

# 或手动启动
MediaServer -c configs/zlm_config.ini -d
```

#### 3. 配置文件说明

- **ZLM 配置文件**: `configs/zlm_config.ini`
  - HTTP 端口: 8081
  - Secret: `UQyXemwV81qnNkuXQSp2eo5txJM35PZr`
  - Hook 回调地址: `http://localhost:8080/api/v1/hooks/*`

- **Gateway 配置文件**: `configs/config.json`
  - ZLM API URL: `http://localhost:8081`
  - Secret: 必须与 ZLM 配置一致

- **前端环境变量**: `frontend/.env.development`
  ```env
  VITE_ZLM_BASE_URL=http://localhost:8081
  VITE_ZLM_SECRET=UQyXemwV81qnNkuXQSp2eo5txJM35PZr
  ```

详细配置说明请参考: [ZLM 配置指南](docs/ZLM_CONFIGURATION.md)

### Gateway 配置

配置文件位于 `configs/config.json`，示例：

```json
{
  "gateway": {
    "http_port": 8080,
    "log_level": "info",
    "log_file": "logs/gateway.log"
  },
  "zlmediakit": {
    "api_url": "http://localhost:8081",
    "secret": "UQyXemwV81qnNkuXQSp2eo5txJM35PZr",
    "rtmp_port": 1935,
    "rtsp_port": 5554,
    "http_port": 8081
  },
  "rtsp": {
    "enabled": true,
    "use_gstreamer": false,
    "ffmpeg_path": "third_party/ffmpeg/macos-arm64/ffmpeg",
    "ffprobe_path": "third_party/ffmpeg/macos-arm64/ffprobe"
  },
  "onvif": {
    "enabled": true,
    "discovery_timeout": 5
  }
}
```

## 开发计划

详见 `../MediaMTX-gateway/docs/ZLMEDIAKIT_CXX_MIGRATION_PLAN.md`

## 与MediaMTX Gateway的关系

- **MediaMTX Gateway**: Go实现，使用MediaMTX作为流媒体服务器（位于 `../MediaMTX-gateway/`）
- **ZLM Gateway**: C++实现，使用ZLMediaKit作为流媒体服务器（当前项目）

两个项目可以并存，用于对比和迁移。

## 许可证

[待定]

