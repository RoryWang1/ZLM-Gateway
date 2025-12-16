#!/bin/bash

# 启动 ZLMediaKit MediaServer 脚本

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"
cd "$PROJECT_ROOT"

echo "=========================================="
echo "启动 ZLMediaKit MediaServer"
echo "=========================================="
echo ""

# 从配置文件读取ZLM端口
ZLM_HTTP_PORT=80
if [ -f "$PROJECT_ROOT/configs/config.json" ]; then
    ZLM_HTTP_PORT=$(python3 -c "import json; f=open('$PROJECT_ROOT/configs/config.json'); d=json.load(f); print(d.get('zlmediakit', {}).get('http_port', 80))" 2>/dev/null || echo "80")
fi

# 检查 ZLMediaKit 是否已运行
if curl --noproxy "*" -s "http://localhost:${ZLM_HTTP_PORT}/index/api/getServerConfig" > /dev/null 2>&1; then
    echo "✅ ZLMediaKit 已在运行 (端口: ${ZLM_HTTP_PORT})"
    exit 0
fi

# 查找 MediaServer 可执行文件
MEDIASERVER=""
if command -v MediaServer &> /dev/null; then
    MEDIASERVER="MediaServer"
elif [ -f "/usr/local/bin/MediaServer" ]; then
    MEDIASERVER="/usr/local/bin/MediaServer"
elif [ -f "$HOME/MediaServer" ]; then
    MEDIASERVER="$HOME/MediaServer"
else
    # 尝试在常见位置查找
    for path in \
        "$PROJECT_ROOT/third_party/zlmediakit/release/darwin/Release/MediaServer" \
        "$PROJECT_ROOT/third_party/zlmediakit/release/mac/Release/MediaServer" \
        "$PROJECT_ROOT/third_party/zlmediakit/release/linux/Release/MediaServer" \
        "$PROJECT_ROOT/third_party/zlmediakit/release/linux/Debug/MediaServer" \
        "/usr/local/MediaServer" \
        "/opt/MediaServer" \
        "$HOME/zlmediakit/release/mac/MediaServer" \
        "$HOME/zlmediakit/Release/mac/MediaServer" \
        "$PROJECT_ROOT/third_party/zlmediakit/MediaServer" \
        "$PROJECT_ROOT/zlmediakit/MediaServer"
    do
        if [ -f "$path" ]; then
            MEDIASERVER="$path"
            break
        fi
    done
fi

if [ -z "$MEDIASERVER" ] || [ ! -f "$MEDIASERVER" ]; then
    echo "❌ 错误: 未找到 MediaServer 可执行文件"
    echo ""
    echo "请安装 ZLMediaKit 或设置 MediaServer 路径："
    echo "  1. 下载 ZLMediaKit: https://github.com/xia-chu/ZLMediaKit"
    echo "  2. 编译并安装 MediaServer"
    echo "  3. 或将 MediaServer 路径添加到 PATH"
    echo ""
    echo "或者手动启动 ZLMediaKit:"
    echo "  MediaServer -d"
    exit 1
fi

echo "找到 MediaServer: $MEDIASERVER"

# 检查配置文件
CONFIG_DIR=""
if [ -d "$PROJECT_ROOT/config" ]; then
    CONFIG_DIR="$PROJECT_ROOT/config"
elif [ -d "$HOME/.zlmediakit" ]; then
    CONFIG_DIR="$HOME/.zlmediakit"
elif [ -d "/etc/zlmediakit" ]; then
    CONFIG_DIR="/etc/zlmediakit"
fi

# 启动 MediaServer
echo ""
echo "🚀 启动 ZLMediaKit MediaServer..."

# 优先使用项目配置文件
ZLM_CONFIG_FILE="$PROJECT_ROOT/configs/zlm_config.ini"
ZLM_CONFIG_TARGET=""

# 确定 MediaServer 的工作目录（可执行文件所在目录）
MEDIASERVER_DIR="$(dirname "$MEDIASERVER")"
MEDIASERVER_ABS_DIR="$(cd "$MEDIASERVER_DIR" && pwd)"
CONFIG_TARGET_PATH="$MEDIASERVER_ABS_DIR/config.ini"

# 如果项目配置文件存在，复制到 MediaServer 目录
if [ -f "$ZLM_CONFIG_FILE" ]; then
    echo "   使用项目配置文件: $ZLM_CONFIG_FILE"
    echo "   同步到: $CONFIG_TARGET_PATH"
    cp "$ZLM_CONFIG_FILE" "$CONFIG_TARGET_PATH"
    ZLM_CONFIG_TARGET="$CONFIG_TARGET_PATH"
    cd "$MEDIASERVER_ABS_DIR"
    nohup "$MEDIASERVER" -d > /dev/null 2>&1 &
elif [ -n "$CONFIG_DIR" ]; then
    echo "   使用配置目录: $CONFIG_DIR"
    cd "$CONFIG_DIR"
    nohup "$MEDIASERVER" -d > /dev/null 2>&1 &
else
    echo "   使用默认配置"
    nohup "$MEDIASERVER" -d > /dev/null 2>&1 &
fi

MEDIASERVER_PID=$!
echo "MediaServer PID: $MEDIASERVER_PID"

# 等待服务启动
echo ""
echo "⏳ 等待 ZLMediaKit 启动..."
for i in {1..10}; do
    sleep 1
    if curl -s "http://localhost:${ZLM_HTTP_PORT}/index/api/getServerConfig" > /dev/null 2>&1; then
        echo "✅ ZLMediaKit 启动成功"
        echo "   HTTP API: http://localhost:${ZLM_HTTP_PORT}"
        echo "   RTSP: rtsp://localhost:8554"
        echo "   RTMP: rtmp://localhost:1935"
        exit 0
    fi
    echo -n "."
done

echo ""
echo "⚠️  ZLMediaKit 可能启动失败，请检查日志"
echo "   PID: $MEDIASERVER_PID"
exit 1

