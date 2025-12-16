# ZLM (ZLMediaKit) 配置指南

本文档说明如何配置 ZLMediaKit 以与 Gateway 正确协作。

## 配置文件位置

- **ZLM 配置文件**: `configs/zlm_config.ini`
- **Gateway 配置文件**: `configs/config.json`
- **前端环境变量**: `frontend/.env.development` 或 `frontend/.env.production`

## 配置检查清单

### 1. ZLM 基础配置 (`configs/zlm_config.ini`)

#### HTTP 服务配置
```ini
[http]
port=8081                    # HTTP 服务端口（必须与 Gateway 配置一致）
rootPath=./www               # Web 根目录
allow_cross_domains=1        # 允许跨域（前端需要）
```

#### API 配置
```ini
[api]
secret=UQyXemwV81qnNkuXQSp2eo5txJM35PZr  # API Secret（必须与 Gateway 配置一致）
snapRoot=./www/snap/         # 快照保存目录
defaultSnap=./www/logo.png  # 默认快照（当流不存在时显示）
```

#### Hook 配置（重要：必须指向 Gateway）
```ini
[hook]
enable=1
on_stream_changed=http://localhost:8080/api/v1/hooks/stream_changed
on_stream_none_reader=http://localhost:8080/api/v1/hooks/stream_none_reader
on_stream_not_found=http://localhost:8080/api/v1/hooks/stream_not_found
on_play=http://localhost:8080/api/v1/hooks/play
on_publish=http://localhost:8080/api/v1/hooks/publish
```

### 2. Gateway 配置 (`configs/config.json`)

```json
{
  "zlmediakit": {
    "api_url": "http://localhost:8081",  // 必须与 ZLM HTTP 端口一致
    "secret": "UQyXemwV81qnNkuXQSp2eo5txJM35PZr",  // 必须与 ZLM secret 一致
    "rtmp_port": 1935,
    "rtsp_port": 5554,
    "http_port": 8081
  }
}
```

### 3. 前端环境变量配置

在 `frontend/.env.development` 或 `frontend/.env.production` 中配置：

```env
# ZLMediaKit 基础URL（用于播放和快照）
VITE_ZLM_BASE_URL=http://localhost:8081

# ZLMediaKit API Secret（用于快照功能）
# 必须与 configs/zlm_config.ini 中的 secret 保持一致
VITE_ZLM_SECRET=UQyXemwV81qnNkuXQSp2eo5txJM35PZr
```

## 配置验证

### 1. 检查 ZLM 是否运行

```bash
curl http://localhost:8081/index/api/getServerConfig?secret=UQyXemwV81qnNkuXQSp2eo5txJM35PZr
```

如果返回 JSON 配置信息，说明 ZLM 运行正常。

### 2. 检查快照功能

```bash
# 测试快照API（需要先有流在运行）
# 使用getSnap API，需要传入HTTP-FLV流的URL
curl "http://localhost:8081/index/api/getSnap?secret=UQyXemwV81qnNkuXQSp2eo5txJM35PZr&url=http://localhost:8081/live/test.live.flv&timeout_sec=5&expire_sec=10"
```

如果返回图片数据（PNG/JPEG），说明快照功能正常。

### 3. 检查 Hook 连接

查看 Gateway 日志，确认 Hook 请求是否正常接收：

```bash
tail -f logs/gateway.log | grep hook
```

## 常见问题

### 1. 快照无法显示

**原因**：
- Secret 配置不一致
- ZLM 未运行
- 流不存在或未启动

**解决方法**：
1. 检查 `configs/zlm_config.ini` 和 `configs/config.json` 中的 secret 是否一致
2. 检查前端环境变量 `VITE_ZLM_SECRET` 是否正确
3. 确认 ZLM 服务已启动
4. 确认流已成功推送到 ZLM

### 2. Hook 回调失败

**原因**：
- Gateway 未运行
- Hook URL 配置错误
- 网络连接问题

**解决方法**：
1. 确认 Gateway 服务已启动（默认端口 8080）
2. 检查 `configs/zlm_config.ini` 中的 Hook URL 是否正确
3. 检查防火墙设置

### 3. 端口冲突

**原因**：
- 端口被其他服务占用

**解决方法**：
1. 检查端口占用：`lsof -i :8081` 或 `netstat -an | grep 8081`
2. 修改配置文件中的端口号
3. 确保所有配置文件中的端口号一致

## 启动 ZLM

### 方法 1：使用启动脚本（推荐）

```bash
./scripts/setup/start_zlmediakit.sh
```

### 方法 2：手动启动

```bash
# 如果 MediaServer 在 PATH 中
MediaServer -d

# 或指定配置文件
MediaServer -c configs/zlm_config.ini -d
```

## 配置同步

启动脚本会自动将 `configs/zlm_config.ini` 同步到 MediaServer 的工作目录。如果手动启动，请确保：

1. 配置文件路径正确
2. 相对路径（如 `./www`）相对于 MediaServer 的工作目录
3. 目录权限正确（MediaServer 需要读写权限）

## 当前配置摘要

- **ZLM HTTP 端口**: 8081
- **ZLM Secret**: `UQyXemwV81qnNkuXQSp2eo5txJM35PZr`
- **Gateway 端口**: 8080
- **快照目录**: `./www/snap/`
- **Hook 回调地址**: `http://localhost:8080/api/v1/hooks/*`

## 安全建议

1. **生产环境**：修改默认 secret 为强密码
2. **网络隔离**：限制 ZLM API 的访问范围
3. **HTTPS**：生产环境建议启用 SSL/TLS
4. **访问控制**：配置 `allow_ip_range` 限制访问来源

