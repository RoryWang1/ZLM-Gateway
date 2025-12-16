# 脚本目录说明

本目录包含项目使用的核心脚本。

## 目录结构

```
scripts/
├── manage.sh                       # 系统管理脚本（启动/停止所有服务）
├── setup/
│   └── start_zlmediakit.sh         # 启动ZLMediaKit服务（manage.sh依赖）
└── test/
    └── protocol_test.sh            # 流管理脚本（创建/删除流）
```

## 核心脚本

### 1. 系统管理脚本 - `manage.sh`

**一键启动/停止所有服务**

**用法**:
```bash
# 启动所有服务 (ZLM, Gateway, 前端)
./scripts/manage.sh start

# 停止所有服务
./scripts/manage.sh stop

# 停止所有服务并清理正在运行的流
./scripts/manage.sh stop --clean-streams

# 查看服务状态
./scripts/manage.sh status
```

**功能**:
- ✅ 启动 ZLMediaKit 服务
- ✅ 启动 Gateway Manager
- ✅ 启动前端开发服务器
- ✅ 停止所有服务（按正确顺序）
- ✅ 可选：停止时自动清理正在运行的流
- ✅ 查看所有服务的运行状态

---

### 2. 流管理脚本 - `test/protocol_test.sh`

**创建和删除流**

**用法**:
```bash
# 创建流（自动创建测试源，验证状态）
./scripts/test/protocol_test.sh create rtsp http-flv

# 创建流（指定源地址）
./scripts/test/protocol_test.sh create rtsp http-flv rtsp://your-source-url

# 删除流
./scripts/test/protocol_test.sh delete live test_rtsp_http-flv_1234567890
```

**支持的协议**:
- 输入协议: `rtsp`, `rtmp`, `http-flv`, `hls`, `dash`, `quic`
- 输出协议: `http-flv`, `hls`, `webrtc`（默认: `http-flv`）

**功能**:
- ✅ 自动创建测试源（RTSP/RTMP协议，使用FFmpeg）
- ✅ 创建流并验证状态
- ✅ 验证播放URL可访问性
- ✅ 保留流（不自动删除）
- ✅ 手动删除流

**特性**:
- 如果没有提供 `source_url`，脚本会自动使用默认值
- 对于 RTSP/RTMP 协议，如果默认源不可用，会自动使用 FFmpeg 创建测试源
- 创建流后会自动验证流状态，确保流正确推送到端口

---

## 环境变量

可以通过环境变量配置脚本行为：

- `GATEWAY_URL`: Gateway API地址（默认: http://localhost:8088）
- `ZLM_URL`: ZLMediaKit API地址（默认: 从配置文件读取）
- `ZLM_SECRET`: ZLMediaKit密钥（默认: 从配置文件读取）
- `FRONTEND_PORT`: 前端端口（默认: 5173）

---

## 使用流程示例

### 启动系统并创建流

```bash
# 1. 启动所有服务
./scripts/manage.sh start

# 2. 创建流（会自动创建测试源）
./scripts/test/protocol_test.sh create rtsp http-flv

# 3. 查看流信息（脚本会显示删除命令）
# 删除命令: ./scripts/test/protocol_test.sh delete live test_rtsp_http-flv_1234567890

# 4. 删除流
./scripts/test/protocol_test.sh delete live test_rtsp_http-flv_1234567890

# 5. 停止所有服务（可选清理流）
./scripts/manage.sh stop --clean-streams
```

---

## 注意事项

1. **配置文件**: 脚本会自动从 `configs/config.json` 读取 ZLM 端口配置
2. **测试源**: RTSP/RTMP 协议会自动创建测试源，HTTP 协议需要手动提供源地址
3. **服务状态**: 使用前确保 Gateway 和 ZLMediaKit 服务已启动（使用 `manage.sh start`）
4. **流保留**: 创建的流会保留，需要手动删除
