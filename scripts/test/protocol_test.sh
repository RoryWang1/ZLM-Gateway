#!/bin/bash

# 流管理脚本
# 用法: 
#   创建流（并验证状态）: ./protocol_test.sh create <protocol> [output_protocol] [source_url]
#   删除流: ./protocol_test.sh delete <app> <stream>

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"
cd "$PROJECT_ROOT"

# 检查依赖
if ! command -v curl > /dev/null 2>&1; then
    echo "❌ 错误: 未找到 curl"
    exit 1
fi

if ! command -v python3 > /dev/null 2>&1; then
    echo "❌ 错误: 未找到 python3"
    exit 1
fi

# 默认配置
ZLM_SECRET="${ZLM_SECRET:-UQyXemwV81qnNkuXQSp2eo5txJM35PZr}"
FRONTEND_URL="${FRONTEND_URL:-http://localhost:5173}"

# 从配置文件读取 Gateway/ZLM 端口
if [ -f "$PROJECT_ROOT/configs/config.json" ]; then
    GATEWAY_HTTP_PORT=$(python3 -c "import json; f=open('$PROJECT_ROOT/configs/config.json'); d=json.load(f); print(d.get('gateway', {}).get('http_port', 8080))" 2>/dev/null || echo "8080")
    GATEWAY_URL="${GATEWAY_URL:-http://localhost:${GATEWAY_HTTP_PORT}}"

    ZLM_HTTP_PORT=$(python3 -c "import json; f=open('$PROJECT_ROOT/configs/config.json'); d=json.load(f); print(d.get('zlmediakit', {}).get('http_port', 8081))" 2>/dev/null || echo "8081")
    ZLM_URL="${ZLM_URL:-http://localhost:${ZLM_HTTP_PORT}}"

    ZLM_RTSP_PORT=$(python3 -c "import json; f=open('$PROJECT_ROOT/configs/config.json'); d=json.load(f); print(d.get('zlmediakit', {}).get('rtsp_port', 8554))" 2>/dev/null || echo "8554")
    ZLM_RTMP_PORT=$(python3 -c "import json; f=open('$PROJECT_ROOT/configs/config.json'); d=json.load(f); print(d.get('zlmediakit', {}).get('rtmp_port', 1935))" 2>/dev/null || echo "1935")
else
    # 回退默认值（历史兼容）
    GATEWAY_URL="${GATEWAY_URL:-http://localhost:8088}"
    ZLM_URL="${ZLM_URL:-http://localhost:8081}"
    ZLM_RTSP_PORT="${ZLM_RTSP_PORT:-8554}"
    ZLM_RTMP_PORT="${ZLM_RTMP_PORT:-1935}"
fi

# 获取FFmpeg路径
get_ffmpeg_path() {
    if [ -f "$PROJECT_ROOT/third_party/ffmpeg/macos-arm64/ffmpeg" ]; then
        echo "$PROJECT_ROOT/third_party/ffmpeg/macos-arm64/ffmpeg"
    elif [ -f "$PROJECT_ROOT/third_party/ffmpeg/linux-x64/ffmpeg" ]; then
        echo "$PROJECT_ROOT/third_party/ffmpeg/linux-x64/ffmpeg"
    elif command -v ffmpeg &> /dev/null; then
        command -v ffmpeg
    else
        echo ""
    fi
}

FFMPEG=$(get_ffmpeg_path)

# 颜色定义
GREEN='\033[0;32m'
RED='\033[0;31m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m'

# ============================================
# 辅助函数：JSON 解析
# ============================================

# 从 JSON 响应中提取字段值
# 用法: json_get_field "$json_string" "field_path"
# 示例: json_get_field "$response" "data.code"
json_get_field() {
    local json_str="$1"
    local field_path="$2"
    echo "$json_str" | python3 -c "
import sys, json
try:
    data = json.load(sys.stdin)
    keys = '$field_path'.split('.')
    value = data
    for key in keys:
        if isinstance(value, dict):
            value = value.get(key)
        elif isinstance(value, list):
            try:
                value = value[int(key)]
            except (ValueError, IndexError):
                value = None
        else:
            value = None
        if value is None:
            break
    print(value if value is not None else '')
except:
    print('')
" 2>/dev/null || echo ""
}

# 检查流是否在 ZLM 中存活
# 用法: check_stream_in_zlm "stream_name"
check_stream_in_zlm() {
    local stream_name="$1"
    local response=$(curl -s --max-time 3 "${ZLM_URL}/index/api/getMediaList?secret=${ZLM_SECRET}" 2>/dev/null)
    echo "$response" | python3 -c "
import sys, json
try:
    data = json.load(sys.stdin)
    streams = data.get('data', [])
    found = [s for s in streams if s.get('stream') == '$stream_name' and s.get('alive', False)]
    print('yes' if found else 'no')
except:
    print('no')
" 2>/dev/null || echo "no"
}

# 通用的等待流就绪函数
# 用法: wait_for_stream_ready "stream_name" "max_wait" "check_function"
wait_for_stream_ready() {
    local stream_name="$1"
    local max_wait=${2:-15}
    local check_func="${3:-check_stream_in_zlm}"
    local progress_msg="${4:-等待流建立}"
    
    echo -n "$progress_msg"
    local wait_count=0
    
    while [ $wait_count -lt $max_wait ]; do
        sleep 1
        local result=$($check_func "$stream_name")
        
        if [ "$result" = "yes" ]; then
            echo -e "\n${GREEN}✓ 流已就绪（等待了 $((wait_count + 1)) 秒）${NC}"
            return 0
        fi
        
        wait_count=$((wait_count + 1))
        if [ $((wait_count % 2)) -eq 0 ]; then
            echo -n "."
        fi
    done
    
    echo ""
    echo -e "${YELLOW}⚠️  流可能尚未完全就绪，但继续尝试...${NC}"
    return 0
}

# 显示用法
usage() {
    cat << EOF
用法: $0 <command> [options]

命令:
  create <protocol> [output_protocol] [source_url]
    创建流并验证状态（流会保留，需要手动删除）
    示例: $0 create rtsp http-flv

  delete <app> <stream>
    删除指定的流
    示例: $0 delete live test_rtsp_http-flv_1234567890

  list
    列出所有流

  cleanup
    清理所有测试源进程和临时文件

协议类型:
  rtsp      - RTSP输入协议
  rtmp      - RTMP输入协议
  http-flv  - HTTP-FLV输入协议
  hls       - HLS输入协议
  dash      - DASH输入协议
  quic      - QUIC输入协议

输出协议 (可选，默认: http-flv):
  http-flv  - HTTP-FLV输出
  hls       - HLS输出
  webrtc    - WebRTC输出

EOF
    exit 1
}

# 检查FFmpeg进程
check_ffmpeg() {
    if pgrep -f "ffmpeg.*rtmp\|ffmpeg.*rtsp" > /dev/null 2>&1; then
        local ffmpeg_pids=$(pgrep -f "ffmpeg.*rtmp\|ffmpeg.*rtsp" | tr '\n' ' ')
        echo -e "${GREEN}✓ FFmpeg 推流进程运行中 (PIDs: $ffmpeg_pids)${NC}"
        return 0
    else
        echo -e "${YELLOW}⚠️  未检测到 FFmpeg 推流进程${NC}"
        return 1
    fi
}

# 检查前端服务
check_frontend() {
    if curl -s --max-time 3 "${FRONTEND_URL}" > /dev/null 2>&1; then
        local status_code=$(curl -s -o /dev/null -w "%{http_code}" --max-time 3 "${FRONTEND_URL}" 2>/dev/null)
        if [ "$status_code" = "200" ]; then
            echo -e "${GREEN}✓ 前端服务运行正常${NC}"
            return 0
        fi
    fi
    echo -e "${RED}❌ 前端服务不可访问: ${FRONTEND_URL}${NC}"
    return 1
}

# 检查服务
check_services() {
    echo -e "${BLUE}══════════════════════════════════════════════════════${NC}"
    echo -e "${BLUE}检查服务状态${NC}"
    echo -e "${BLUE}══════════════════════════════════════════════════════${NC}"
    echo ""
    
    local gateway_ok=0
    local zlm_ok=0
    local frontend_ok=0
    
    # 检查Gateway
    if curl -s --max-time 3 "${GATEWAY_URL}/health" > /dev/null 2>&1; then
        local health=$(curl -s --max-time 3 "${GATEWAY_URL}/health" 2>/dev/null)
        if echo "$health" | grep -q "healthy\|status"; then
            echo -e "${GREEN}✓ Gateway 服务运行正常${NC}"
            gateway_ok=1
        else
            echo -e "${RED}❌ Gateway 服务响应异常${NC}"
        fi
    else
        echo -e "${RED}❌ Gateway API 不可用: ${GATEWAY_URL}${NC}"
    fi
    
    # 检查ZLMediaKit
    if curl -s --max-time 3 "${ZLM_URL}/index/api/getServerConfig?secret=${ZLM_SECRET}" > /dev/null 2>&1; then
        echo -e "${GREEN}✓ ZLMediaKit 服务运行正常${NC}"
        zlm_ok=1
    else
        echo -e "${RED}❌ ZLMediaKit 不可用: ${ZLM_URL}${NC}"
    fi
    
    # 检查前端服务
    if check_frontend; then
        frontend_ok=1
    fi
    
    # 检查FFmpeg（可选）
    check_ffmpeg || true
    
    echo ""
    
    # 核心服务必须运行（Gateway + ZLM）
    if [ $gateway_ok -eq 0 ] || [ $zlm_ok -eq 0 ]; then
        echo -e "${RED}❌ 核心服务不可用，无法继续测试${NC}"
        exit 1
    fi
    
    # 前端服务仅用于人工验证，不再作为 CLI 测试的硬依赖
    if [ $frontend_ok -eq 0 ]; then
        echo -e "${YELLOW}⚠️  前端服务不可用，将跳过前端相关验证${NC}"
        echo -e "${YELLOW}   如需在浏览器中确认，请单独启动前端: cd frontend && npm run dev${NC}"
    fi
    
    echo -e "${GREEN}✓ 后端核心服务检查完成${NC}"
    echo ""
}

# 获取默认源地址
get_default_source() {
    local protocol=$1
    
    case "$protocol" in
        rtsp)
            # 使用从配置文件读取的端口
            local rtsp_port="${ZLM_RTSP_PORT:-8554}"
            echo "rtsp://127.0.0.1:${rtsp_port}/live/test_source"
            ;;
        rtmp)
            # 使用从配置文件读取的端口
            local rtmp_port="${ZLM_RTMP_PORT:-1935}"
            echo "rtmp://127.0.0.1:${rtmp_port}/live/test_source"
            ;;
        http-flv)
            echo "http://127.0.0.1:8081/live/test_source.live.flv"
            ;;
        hls)
            echo "http://127.0.0.1:8081/live/test_source/hls.m3u8"
            ;;
        dash)
            echo "http://127.0.0.1:8890/live/dash_test.mpd"
            ;;
        quic)
            echo "http://127.0.0.1:8891/live/quic_test.mpd"
            ;;
        *)
            echo ""
            ;;
    esac
}

# 自动创建RTSP测试源
auto_create_rtsp_source() {
    local stream_name=${1:-test_source}
    
    if [ -z "$FFMPEG" ]; then
        echo -e "${RED}❌ 未找到 FFmpeg，无法自动创建测试源${NC}"
        return 1
    fi
    
    echo -e "${BLUE}自动创建RTSP测试源: ${stream_name}${NC}"
    
    # 停止旧进程
    pkill -f "ffmpeg.*${stream_name}" 2>/dev/null || true
    sleep 1
    
    local rtsp_url="rtsp://127.0.0.1:${ZLM_RTSP_PORT}/live/${stream_name}?secret=${ZLM_SECRET}"
    
    # 构建FFmpeg命令（使用测试源）
    local ffmpeg_cmd="$FFMPEG -re -f lavfi -i testsrc=size=1280x720:rate=25 -f lavfi -i sine=frequency=1000:sample_rate=44100"
    ffmpeg_cmd="$ffmpeg_cmd -c:v libx264 -pix_fmt yuv420p -preset veryfast -tune zerolatency -g 30 -b:v 1.5M -maxrate 1.5M -bufsize 3M"
    ffmpeg_cmd="$ffmpeg_cmd -vf scale=854:480 -c:a aac -b:a 64k -ar 44100 -ac 2 -fpsmax 25"
    ffmpeg_cmd="$ffmpeg_cmd -f rtsp -rtsp_transport tcp \"$rtsp_url\""
    
    # 启动FFmpeg进程
    eval "nohup $ffmpeg_cmd > /tmp/rtsp_source_${stream_name}.log 2>&1 &"
    local pid=$!
    echo "$pid" > "/tmp/rtsp_source_${stream_name}.pid"
    
    echo -e "${GREEN}✓ RTSP测试源已启动 (PID: $pid)${NC}"
    
    # 等待流在ZLM中激活（最多等待15秒）
    wait_for_stream_ready "$stream_name" 15 "check_stream_in_zlm" "等待流建立"
    return 0
}

# 查找可用端口
find_available_port() {
    local start_port=$1
    local port=$start_port
    while [ $port -lt $((start_port + 100)) ]; do
        if ! lsof -i :$port > /dev/null 2>&1; then
            echo $port
            return 0
        fi
        port=$((port + 1))
    done
    echo ""
    return 1
}

# 自动创建HTTP-FLV测试源
auto_create_httpflv_source() {
    local stream_name=${1:-test_source}
    local http_port=${2:-8888}
    # 允许通过环境变量覆盖 HTTP 源对外暴露的 Host，方便 ZLM 在容器/异机场景下访问
    # 默认使用 127.0.0.1（本机）
    local http_host="${HTTPFLV_SOURCE_HOST:-127.0.0.1}"
    
    if [ -z "$FFMPEG" ]; then
        echo -e "${RED}❌ 未找到 FFmpeg，无法自动创建测试源${NC}"
        return 1
    fi
    
    echo -e "${BLUE}自动创建HTTP-FLV Live测试源: ${stream_name}${NC}"
    
    # 停止旧进程
    pkill -f "ffmpeg.*listen.*${stream_name}" 2>/dev/null || true
    pkill -f "ffmpeg.*http.*${stream_name}" 2>/dev/null || true
    pkill -f "python.*http.server.*${http_port}" 2>/dev/null || true
    sleep 1
    
    # 检查端口是否可用，如果不可用则查找可用端口
    if lsof -i :$http_port > /dev/null 2>&1; then
        local new_port=$(find_available_port $http_port)
        if [ -z "$new_port" ]; then
            echo -e "${RED}❌ 无法找到可用端口（尝试了 $http_port-$((http_port + 100))）${NC}"
            return 1
        fi
        echo -e "${YELLOW}⚠️  端口 $http_port 被占用，使用端口 $new_port${NC}"
        http_port=$new_port
    fi
    
    # 使用项目中的静态 FLV 测试源 + 本地 HTTP 服务器，避免 FFmpeg -listen 1 的一次性行为
    local httpflv_source_dir="$PROJECT_ROOT/test_sources"
    local flv_source="${httpflv_source_dir}/sky_with_clock.flv"
    if [ ! -f "$flv_source" ]; then
        echo -e "${RED}❌ 未找到 HTTP-FLV 测试源文件: $flv_source${NC}"
        return 1
    fi

    # 在测试目录下为当前 stream_name 建一个符号链接，统一使用 ${stream_name}.flv 作为访问路径
    ( cd "$httpflv_source_dir" && ln -sf "sky_with_clock.flv" "${stream_name}.flv" )

    # 启动HTTP服务器（在测试源目录中）
    cd "$httpflv_source_dir"
    python3 -m http.server "$http_port" > /tmp/http_server_${stream_name}.log 2>&1 &
    local http_pid=$!
    echo "$http_pid" > "/tmp/http_server_${stream_name}_http.pid"
    
    echo -e "${GREEN}✓ HTTP-FLV测试源已启动 (HTTP PID: $http_pid)${NC}"
    echo "URL: http://${http_host}:${http_port}/${stream_name}.flv"
    
    # 将实际使用的端口保存到临时文件，让调用者知道
    if [ -n "$3" ]; then
        echo "$http_port" > "/tmp/http_port_${stream_name}.txt"
    fi
    
    # 等待HTTP服务器启动
    sleep 2
    
    # 简单检查 HTTP-FLV 是否可访问
    local http_status=$(curl -s -o /dev/null -w "%{http_code}" "http://${http_host}:${http_port}/${stream_name}.flv" 2>/dev/null || echo "000")
        http_status=$(echo "$http_status" | grep -oE '[0-9]{3}' | head -1 || echo "000")
        if [ "$http_status" = "200" ] || [ "$http_status" = "206" ]; then
        echo -e "${GREEN}✓ HTTP-FLV测试源可访问${NC}"
            return 0
    else
        echo -e "${YELLOW}⚠️  HTTP-FLV测试源可能尚未完全就绪 (HTTP $http_status)${NC}"
    return 0
    fi
}

# 自动创建HLS测试源
auto_create_hls_source() {
    local stream_name=${1:-test_source}
    local http_port=${2:-8888}
    
    echo -e "${BLUE}自动创建HLS测试源: ${stream_name}${NC}"
    
    # 停止旧进程
    pkill -f "python.*http.server.*${http_port}" 2>/dev/null || true
    sleep 1
    
    # 检查端口是否可用，如果不可用则查找可用端口
    if lsof -i :$http_port > /dev/null 2>&1; then
        local new_port=$(find_available_port $http_port)
        if [ -z "$new_port" ]; then
            echo -e "${RED}❌ 无法找到可用端口（尝试了 $http_port-$((http_port + 100))）${NC}"
            return 1
        fi
        echo -e "${YELLOW}⚠️  端口 $http_port 被占用，使用端口 $new_port${NC}"
        http_port=$new_port
    fi
    
    # 使用项目中的静态 HLS 测试源
    local hls_source_dir="$PROJECT_ROOT/test_sources/hls_test"
    if [ ! -d "$hls_source_dir" ] || [ ! -f "$hls_source_dir/stream.m3u8" ]; then
        echo -e "${RED}❌ 未找到 HLS 测试源目录: $hls_source_dir${NC}"
        return 1
    fi
    
    # 启动HTTP服务器（在测试源目录中）
    cd "$hls_source_dir"
    python3 -m http.server "$http_port" > /tmp/http_server_${stream_name}.log 2>&1 &
    local http_pid=$!
    echo "$http_pid" > "/tmp/http_server_${stream_name}_http.pid"
    
    echo -e "${GREEN}✓ HLS测试源已启动 (HTTP PID: $http_pid)${NC}"
    echo "URL: http://127.0.0.1:${http_port}/stream.m3u8"
    
    # 将实际使用的端口保存到临时文件，让调用者知道
    if [ -n "$3" ]; then
        echo "$http_port" > "/tmp/http_port_${stream_name}.txt"
    fi
    
    # 等待HTTP服务器启动
    sleep 2
    
    # 检查HLS是否可访问
    local http_status=$(curl -s -o /dev/null -w "%{http_code}" "http://127.0.0.1:${http_port}/stream.m3u8" 2>/dev/null || echo "000")
    if [ "$http_status" = "200" ]; then
        echo -e "${GREEN}✓ HLS测试源可访问${NC}"
        return 0
    else
        echo -e "${YELLOW}⚠️  HLS测试源可能尚未完全就绪 (HTTP $http_status)${NC}"
        return 0
    fi
}

# 自动创建DASH测试源
auto_create_dash_source() {
    local stream_name=${1:-test_source}
    local http_port=${2:-8890}
    
    echo -e "${BLUE}自动创建DASH测试源: ${stream_name}${NC}"
    
    # 停止旧进程
    pkill -f "python.*http.server.*${http_port}" 2>/dev/null || true
    sleep 1
    
    # 检查端口是否可用，如果不可用则查找可用端口
    if lsof -i :$http_port > /dev/null 2>&1; then
        local new_port=$(find_available_port $http_port)
        if [ -z "$new_port" ]; then
            echo -e "${RED}❌ 无法找到可用端口（尝试了 $http_port-$((http_port + 100))）${NC}"
            return 1
        fi
        echo -e "${YELLOW}⚠️  端口 $http_port 被占用，使用端口 $new_port${NC}"
        http_port=$new_port
    fi
    
    # 使用项目中的静态 DASH 测试源
    local dash_source_dir="$PROJECT_ROOT/test_sources/dash_sky"
    if [ ! -d "$dash_source_dir" ] || [ ! -f "$dash_source_dir/manifest.mpd" ]; then
        echo -e "${RED}❌ 未找到 DASH 测试源目录: $dash_source_dir${NC}"
        return 1
    fi
    
    # 启动HTTP服务器（在测试源目录中）
    cd "$dash_source_dir"
    python3 -m http.server "$http_port" > /tmp/http_server_${stream_name}.log 2>&1 &
    local http_pid=$!
    echo "$http_pid" > "/tmp/http_server_${stream_name}_http.pid"
    
    echo -e "${GREEN}✓ DASH测试源已启动 (HTTP PID: $http_pid)${NC}"
    echo "URL: http://127.0.0.1:${http_port}/manifest.mpd"
    
    # 将实际使用的端口保存到临时文件，让调用者知道
    if [ -n "$3" ]; then
        echo "$http_port" > "/tmp/http_port_${stream_name}.txt"
    fi
    
    # 等待HTTP服务器启动
    sleep 2
    
    # 检查DASH是否可访问
    local http_status=$(curl -s -o /dev/null -w "%{http_code}" "http://127.0.0.1:${http_port}/manifest.mpd" 2>/dev/null || echo "000")
    if [ "$http_status" = "200" ]; then
        echo -e "${GREEN}✓ DASH测试源可访问${NC}"
        return 0
    else
        echo -e "${YELLOW}⚠️  DASH测试源可能尚未完全就绪 (HTTP $http_status)${NC}"
        return 0
    fi
}

# 自动创建RTMP测试源
auto_create_rtmp_source() {
    local stream_name=${1:-test_source}
    
    if [ -z "$FFMPEG" ]; then
        echo -e "${RED}❌ 未找到 FFmpeg，无法自动创建测试源${NC}"
        return 1
    fi
    
    echo -e "${BLUE}自动创建RTMP测试源: ${stream_name}${NC}"
    
    # 停止旧进程
    pkill -f "ffmpeg.*${stream_name}" 2>/dev/null || true
    sleep 1
    
    local rtmp_url="rtmp://127.0.0.1:${ZLM_RTMP_PORT}/live/${stream_name}?secret=${ZLM_SECRET}"
    
    # 构建FFmpeg命令（使用测试源）
    local ffmpeg_cmd="$FFMPEG -re -f lavfi -i testsrc=size=1280x720:rate=25 -f lavfi -i sine=frequency=1000:sample_rate=44100"
    ffmpeg_cmd="$ffmpeg_cmd -c:v libx264 -preset veryfast -tune zerolatency -g 30 -b:v 1.5M -maxrate 1.5M -bufsize 3M"
    ffmpeg_cmd="$ffmpeg_cmd -vf scale=854:480 -c:a aac -b:a 64k -ar 44100 -ac 2 -fpsmax 25"
    ffmpeg_cmd="$ffmpeg_cmd -f flv \"$rtmp_url\""
    
    # 启动FFmpeg进程
    eval "nohup $ffmpeg_cmd > /tmp/rtmp_source_${stream_name}.log 2>&1 &"
    local pid=$!
    echo "$pid" > "/tmp/rtmp_source_${stream_name}.pid"
    
    echo -e "${GREEN}✓ RTMP测试源已启动 (PID: $pid)${NC}"
    
    # 等待流在ZLM中激活（最多等待15秒）
    wait_for_stream_ready "$stream_name" 15 "check_stream_in_zlm" "等待流建立"
    return 0
}

# 列出所有流
list_streams() {
    echo -e "${BLUE}══════════════════════════════════════════════════════${NC}"
    echo -e "${BLUE}流列表${NC}"
    echo -e "${BLUE}══════════════════════════════════════════════════════${NC}"
    echo ""
    
    local streams_response=$(curl -s --max-time 5 "${GATEWAY_URL}/api/v1/streams" 2>/dev/null)
    
    if [ -z "$streams_response" ]; then
        echo -e "${RED}❌ 无法获取流列表${NC}"
        return 1
    fi
    
    local stream_count=$(echo "$streams_response" | python3 -c "
import sys, json
try:
    data = json.load(sys.stdin)
    streams = data.get('data', {}).get('streams', [])
    print(len(streams))
except:
    print(0)
" 2>/dev/null || echo "0")
    
    if [ "$stream_count" = "0" ]; then
        echo -e "${YELLOW}⚠️  当前没有活跃的流${NC}"
        echo ""
        return 0
    fi
    
    echo -e "${GREEN}找到 $stream_count 个流:${NC}"
    echo ""
    
    echo "$streams_response" | python3 -c "
import sys, json
try:
    data = json.load(sys.stdin)
    streams = data.get('data', {}).get('streams', [])
    for s in streams:
        app = s.get('app', 'N/A')
        stream = s.get('stream', 'N/A')
        status = s.get('status', -1)
        protocol = s.get('protocol', 'N/A')
        output = s.get('output_protocol', 'N/A')
        status_text = {0: 'Stopped', 1: 'Starting', 2: 'Running', 4: 'Error'}.get(status, f'Unknown({status})')
        status_color = {0: 'YELLOW', 1: 'YELLOW', 2: 'GREEN', 4: 'RED'}.get(status, 'YELLOW')
        print(f'  {app}/{stream}')
        print(f'    协议: {protocol} → {output}')
        print(f'    状态: {status_text}')
        print('')
except Exception as e:
    print(f'解析错误: {e}')
" 2>/dev/null || echo -e "${RED}❌ 解析流列表失败${NC}"
    
    echo ""
    return 0
}

# 清理测试资源
cleanup_resources() {
    echo -e "${BLUE}══════════════════════════════════════════════════════${NC}"
    echo -e "${BLUE}清理测试资源${NC}"
    echo -e "${BLUE}══════════════════════════════════════════════════════${NC}"
    echo ""
    
    local cleaned=0
    
    # 清理测试源进程
    echo -e "${BLUE}清理测试源进程...${NC}"
    
    # RTSP/RTMP 测试源
    if pkill -f "ffmpeg.*test_source" 2>/dev/null; then
        echo -e "${GREEN}✓ 已停止 RTSP/RTMP 测试源进程${NC}"
        cleaned=$((cleaned + 1))
    fi
    
    # HTTP-FLV 测试源
    if pkill -f "ffmpeg.*listen.*test_source" 2>/dev/null; then
        echo -e "${GREEN}✓ 已停止 HTTP-FLV 测试源进程${NC}"
        cleaned=$((cleaned + 1))
    fi
    
    # HTTP 服务器（HLS/DASH）
    if pkill -f "python.*http.server.*8888\|python.*http.server.*8890\|python.*http.server.*8891" 2>/dev/null; then
        echo -e "${GREEN}✓ 已停止 HTTP 测试源服务器${NC}"
        cleaned=$((cleaned + 1))
    fi
    
    # 清理临时文件
    echo ""
    echo -e "${BLUE}清理临时文件...${NC}"
    
    local temp_files=(
        "/tmp/rtsp_source_*.pid"
        "/tmp/rtmp_source_*.pid"
        "/tmp/httpflv_live_source_*.pid"
        "/tmp/http_server_*_http.pid"
        "/tmp/http_port_*.txt"
        "/tmp/rtsp_source_*.log"
        "/tmp/rtmp_source_*.log"
        "/tmp/httpflv_live_source_*.log"
        "/tmp/http_server_*.log"
    )
    
    for pattern in "${temp_files[@]}"; do
        if ls $pattern 2>/dev/null | head -1 > /dev/null; then
            rm -f $pattern 2>/dev/null && cleaned=$((cleaned + 1))
        fi
    done
    
    if [ $cleaned -gt 0 ]; then
        echo -e "${GREEN}✓ 已清理 $cleaned 个资源${NC}"
    else
        echo -e "${YELLOW}⚠️  没有找到需要清理的资源${NC}"
    fi
    
    echo ""
    return 0
}

# 删除流
delete_stream() {
    local app=$1
    local stream=$2
    
    if [ -z "$app" ] || [ -z "$stream" ]; then
        echo -e "${RED}❌ 错误: 需要提供 app 和 stream 参数${NC}"
        echo "用法: $0 delete <app> <stream>"
        exit 1
    fi
    
    echo -e "${BLUE}删除流: ${app}/${stream}${NC}"
    
    local response=$(curl -s -w "\nHTTP_CODE:%{http_code}" -X DELETE "${GATEWAY_URL}/api/v1/streams/${app}/${stream}" 2>&1)
    local http_code=$(echo "$response" | grep "HTTP_CODE:" | cut -d: -f2)
    local body=$(echo "$response" | grep -v "HTTP_CODE:")
    
    if [ "$http_code" = "200" ]; then
        local code=$(echo "$body" | python3 -c "import sys, json; data=json.load(sys.stdin); print(data.get('code', -1))" 2>/dev/null || echo "-1")
        if [ "$code" = "0" ]; then
            echo -e "${GREEN}✓ 流删除成功: ${app}/${stream}${NC}"
            return 0
        else
            echo -e "${YELLOW}⚠️  流删除返回 code=$code${NC}"
            echo "$body" | python3 -m json.tool 2>/dev/null || echo "$body"
            return 1
        fi
    else
        echo -e "${RED}❌ 删除流失败 (HTTP $http_code)${NC}"
        echo "$body"
        return 1
    fi
}

# 创建流
create_stream() {
    local input_protocol=$1
    local output_protocol=${2:-http-flv}
    local source_url=$3
    
    # 如果没有提供源地址，对于RTSP/RTMP自动创建测试源
    if [ -z "$source_url" ]; then
        case "$input_protocol" in
            rtsp)
                local stream_name="test_source"
                local rtsp_port="${ZLM_RTSP_PORT:-8554}"
                source_url="rtsp://127.0.0.1:${rtsp_port}/live/${stream_name}"
                
                echo -e "${BLUE}未提供源地址，自动创建RTSP测试源...${NC}"
                if auto_create_rtsp_source "$stream_name"; then
                    echo -e "${GREEN}✓ 测试源创建成功，继续创建流${NC}"
                    echo ""
                else
                    echo -e "${RED}❌ 无法自动创建测试源${NC}"
                    echo ""
                    echo -e "${YELLOW}请手动提供可用的源地址：${NC}"
                    echo "  提供源地址: $0 create rtsp $output_protocol rtsp://your-source-url"
                    echo ""
                    return 1
                fi
                ;;
            rtmp)
                local stream_name="test_source"
                local rtmp_port="${ZLM_RTMP_PORT:-1935}"
                source_url="rtmp://127.0.0.1:${rtmp_port}/live/${stream_name}"
                
                echo -e "${BLUE}未提供源地址，自动创建RTMP测试源...${NC}"
                if auto_create_rtmp_source "$stream_name"; then
                    echo -e "${GREEN}✓ 测试源创建成功，继续创建流${NC}"
                    echo ""
                else
                    echo -e "${RED}❌ 无法自动创建测试源${NC}"
                    echo ""
                    echo -e "${YELLOW}请手动提供可用的源地址：${NC}"
                    echo "  提供源地址: $0 create rtmp $output_protocol rtmp://your-source-url"
                    echo ""
                    return 1
                fi
                ;;
                http-flv)
                    local stream_name="test_source"
                    local http_port=8888
                    
                    echo -e "${BLUE}未提供源地址，自动创建HTTP-FLV测试源...${NC}"
                    if auto_create_httpflv_source "$stream_name" "$http_port" "save_port"; then
                        # 获取实际使用的端口
                        if [ -f "/tmp/http_port_${stream_name}.txt" ]; then
                            http_port=$(cat "/tmp/http_port_${stream_name}.txt")
                        fi
                        source_url="http://127.0.0.1:${http_port}/${stream_name}.flv"
                        echo -e "${GREEN}✓ 测试源创建成功，继续创建流${NC}"
                        echo ""
                    else
                        echo -e "${RED}❌ 无法自动创建测试源${NC}"
                        echo ""
                        echo -e "${YELLOW}请手动提供可用的源地址：${NC}"
                        echo "  提供源地址: $0 create http-flv $output_protocol http://your-source-url"
                        echo ""
                        return 1
                    fi
                    ;;
                hls)
                    local stream_name="test_source"
                    local http_port=8888
                    
                    echo -e "${BLUE}未提供源地址，自动创建HLS测试源...${NC}"
                    if auto_create_hls_source "$stream_name" "$http_port" "save_port"; then
                        # 获取实际使用的端口
                        if [ -f "/tmp/http_port_${stream_name}.txt" ]; then
                            http_port=$(cat "/tmp/http_port_${stream_name}.txt")
                        fi
                        source_url="http://127.0.0.1:${http_port}/stream.m3u8"
                        echo -e "${GREEN}✓ 测试源创建成功，继续创建流${NC}"
                        echo ""
                    else
                        echo -e "${RED}❌ 无法自动创建测试源${NC}"
                        echo ""
                        echo -e "${YELLOW}请手动提供可用的源地址：${NC}"
                        echo "  提供源地址: $0 create hls $output_protocol http://your-source-url"
                        echo ""
                        return 1
                    fi
                    ;;
                dash)
                    local stream_name="test_source"
                    local http_port=8890
                    
                    echo -e "${BLUE}未提供源地址，自动创建DASH测试源...${NC}"
                    if auto_create_dash_source "$stream_name" "$http_port" "save_port"; then
                        # 获取实际使用的端口
                        if [ -f "/tmp/http_port_${stream_name}.txt" ]; then
                            http_port=$(cat "/tmp/http_port_${stream_name}.txt")
                        fi
                        source_url="http://127.0.0.1:${http_port}/manifest.mpd"
                        echo -e "${GREEN}✓ 测试源创建成功，继续创建流${NC}"
                        echo -e "${YELLOW}⚠️  注意：DASH 流启动可能需要更多时间（FFprobe 探测）${NC}"
                        echo ""
                    else
                        echo -e "${RED}❌ 无法自动创建测试源${NC}"
                        echo ""
                        echo -e "${YELLOW}请手动提供可用的源地址：${NC}"
                        echo "  提供源地址: $0 create dash $output_protocol http://your-source-url"
                        echo ""
                        return 1
                    fi
                    ;;
                quic)
                    # QUIC使用DASH格式
                    local stream_name="test_source"
                    local http_port=8891
                    
                    echo -e "${BLUE}未提供源地址，自动创建QUIC测试源（使用DASH格式）...${NC}"
                    if auto_create_dash_source "$stream_name" "$http_port" "save_port"; then
                        # 获取实际使用的端口
                        if [ -f "/tmp/http_port_${stream_name}.txt" ]; then
                            http_port=$(cat "/tmp/http_port_${stream_name}.txt")
                        fi
                        source_url="http://127.0.0.1:${http_port}/manifest.mpd"
                        echo -e "${GREEN}✓ 测试源创建成功，继续创建流${NC}"
                        echo ""
                    else
                        echo -e "${RED}❌ 无法自动创建测试源${NC}"
                        echo ""
                        echo -e "${YELLOW}请手动提供可用的源地址：${NC}"
                        echo "  提供源地址: $0 create quic $output_protocol http://your-source-url"
                        echo ""
            return 1
        fi
                    ;;
            *)
                echo -e "${RED}❌ 无法确定默认源地址，请手动提供 source_url${NC}"
                return 1
                ;;
        esac
    fi
    
    # 生成测试流名
    local stream_name="test_${input_protocol}_${output_protocol}_$(date +%s)"
    local app="live"
    
    echo -e "${BLUE}创建流: ${app}/${stream_name}${NC}"
    echo "输入协议: $input_protocol"
    echo "输出协议: $output_protocol"
    echo "源地址: $source_url"
    echo ""
    
    # 创建流
    local response=$(curl -s -w "\nHTTP_CODE:%{http_code}" -X POST "${GATEWAY_URL}/api/v1/streams/start" \
        -H 'Content-Type: application/json' \
        -d "{
            \"protocol\": \"${input_protocol}\",
            \"output_protocol\": \"${output_protocol}\",
            \"source_url\": \"${source_url}\",
            \"app\": \"${app}\",
            \"stream\": \"${stream_name}\"
        }" 2>&1)
    
    local http_code=$(echo "$response" | grep "HTTP_CODE:" | cut -d: -f2)
    local body=$(echo "$response" | grep -v "HTTP_CODE:")
    
    if [ "$http_code" != "200" ]; then
        echo -e "${RED}❌ HTTP错误: $http_code${NC}"
        echo "$body"
        return 1
    fi
    
    local code=$(echo "$body" | python3 -c "import sys, json; data=json.load(sys.stdin); print(data.get('code', -1))" 2>/dev/null || echo "-1")
    
    if [ "$code" != "0" ]; then
        echo -e "${RED}❌ 流创建失败 (code=$code)${NC}"
        echo "$body" | python3 -m json.tool 2>/dev/null || echo "$body"
        return 1
    fi
    
    echo -e "${GREEN}✓ 流创建成功: ${app}/${stream_name}${NC}"
    echo ""
    
    # 返回流信息（通过全局变量）
    CREATED_APP="$app"
    CREATED_STREAM="$stream_name"
    CREATED_PROTOCOL="$input_protocol"
    CREATED_OUTPUT="$output_protocol"
    
    return 0
}

# 验证流格式和状态
verify_stream() {
    local app=$1
    local stream=$2
    local expected_output=$3
    
    echo -e "${BLUE}验证流状态和格式...${NC}"
    
    # 轮询检查流状态，而不是固定等待
    local max_wait=30
    if [ "$CREATED_PROTOCOL" = "dash" ] || [ "$CREATED_PROTOCOL" = "quic" ]; then
        max_wait=45
    fi
    
    echo "等待流启动（最多 ${max_wait} 秒）..."
    local wait_count=0
    local stream_ready=0
    
    while [ $wait_count -lt $max_wait ]; do
        # 检查流状态
        local status_response=$(curl -s --max-time 3 "${GATEWAY_URL}/api/v1/streams/${app}/${stream}" 2>/dev/null)
        
        if [ -n "$status_response" ]; then
            local status=$(echo "$status_response" | python3 -c "
import sys, json
try:
    data = json.load(sys.stdin)
    stream_data = data.get('data', {})
    status = stream_data.get('status', -1)
    print(status)
except:
    print('-1')
" 2>/dev/null || echo "-1")
            
            # 状态 2 表示 Running，1 表示 Starting
            if [ "$status" = "2" ] || [ "$status" = "1" ]; then
                stream_ready=1
                echo -e "\n${GREEN}✓ 流已就绪（等待了 $((wait_count + 1)) 秒）${NC}"
                break
            fi
        fi
        
        wait_count=$((wait_count + 1))
        if [ $((wait_count % 3)) -eq 0 ]; then
            echo -n "."
        fi
        sleep 1
    done
    
    if [ $stream_ready -eq 0 ]; then
        echo -e "\n${YELLOW}⚠️  流可能尚未完全就绪，但继续验证...${NC}"
    fi
    
    echo ""
    
    # 检查流状态
    local status_response=$(curl -s "${GATEWAY_URL}/api/v1/streams/${app}/${stream}" 2>/dev/null)
    
    if [ -z "$status_response" ]; then
        echo -e "${RED}❌ 无法获取流信息${NC}"
        return 1
    fi
    
    local status=$(echo "$status_response" | python3 -c "
import sys, json
try:
    data = json.load(sys.stdin)
    stream_data = data.get('data', {})
    status = stream_data.get('status', -1)
    output_protocol = stream_data.get('output_protocol', '')
    gateway_type = stream_data.get('gateway_type', '')
    zlm_alive = stream_data.get('zlm_alive', False)
    source_url = stream_data.get('source_url', '')
    
    print(f'status={status}')
    print(f'output_protocol={output_protocol}')
    print(f'gateway_type={gateway_type}')
    print(f'zlm_alive={zlm_alive}')
    print(f'source_url={source_url}')
except:
    print('status=-1')
" 2>/dev/null || echo "status=-1")
    
    local stream_status=$(echo "$status" | grep "^status=" | cut -d= -f2)
    local actual_output=$(echo "$status" | grep "^output_protocol=" | cut -d= -f2)
    local gateway_type=$(echo "$status" | grep "^gateway_type=" | cut -d= -f2)
    local zlm_alive=$(echo "$status" | grep "^zlm_alive=" | cut -d= -f2)
    local source_url=$(echo "$status" | grep "^source_url=" | cut -d= -f2)
    
    # 验证output_protocol
    if [ "$actual_output" != "$expected_output" ]; then
        echo -e "${RED}❌ output_protocol 不匹配 (期望: $expected_output, 实际: $actual_output)${NC}"
        return 1
    else
        echo -e "${GREEN}✓ output_protocol 正确: $actual_output${NC}"
    fi
    
    # 验证状态
    case "$stream_status" in
        2)
            echo -e "${GREEN}✓ 流状态: Running${NC}"
            ;;
        1)
            echo -e "${YELLOW}⚠️  流状态: Starting (可能需要更多时间)${NC}"
            ;;
        4)
            echo -e "${RED}❌ 流状态: Error${NC}"
            return 1
            ;;
        *)
            echo -e "${YELLOW}⚠️  流状态: $stream_status${NC}"
            ;;
    esac
    
    echo "Gateway类型: $gateway_type"
    echo "ZLMediaKit中是否存活: $zlm_alive"
    echo "源地址: $source_url"
    echo ""
    
    # 验证播放URL
    local play_url=""
    case "$expected_output" in
        http-flv)
            play_url="${ZLM_URL}/${app}/${stream}.live.flv"
            ;;
        hls)
            play_url="${ZLM_URL}/${app}/${stream}/hls.m3u8"
            ;;
        webrtc)
            play_url="${ZLM_URL}/index/api/webrtc?app=${app}&stream=${stream}&type=play"
            ;;
    esac
    
    if [ -n "$play_url" ]; then
        echo "播放URL: $play_url"
        
        # 对于HTTP-FLV和HLS，检查URL是否可访问
        if [ "$expected_output" = "http-flv" ] || [ "$expected_output" = "hls" ]; then
            local http_status=$(curl -s -o /dev/null -w "%{http_code}" --max-time 5 "$play_url" 2>/dev/null | tr -d '\n' || echo "000")
            # 清理状态码，只保留数字部分
            http_status=$(echo "$http_status" | grep -oE '[0-9]{3}' | head -1 || echo "000")
            if [ "$http_status" = "200" ] || [ "$http_status" = "206" ]; then
                echo -e "${GREEN}✓ 播放URL可访问 (HTTP $http_status)${NC}"
            else
                echo -e "${YELLOW}⚠️  播放URL返回 HTTP $http_status (流可能还在初始化)${NC}"
            fi
        fi
    fi
    
    # 验证前端显示（通过API检查流是否在前端列表中）
    echo ""
    echo -e "${BLUE}验证前端显示...${NC}"
    local streams_response=$(curl -s "${GATEWAY_URL}/api/v1/streams" 2>/dev/null)
    if echo "$streams_response" | python3 -c "
import sys, json
try:
    data = json.load(sys.stdin)
    streams = data.get('data', {}).get('streams', [])
    found = [s for s in streams if s.get('app') == '$app' and s.get('stream') == '$stream']
    sys.exit(0 if found else 1)
except:
    sys.exit(1)
" 2>/dev/null; then
        echo -e "${GREEN}✓ 流已在前端流列表中${NC}"
    else
        echo -e "${YELLOW}⚠️  流可能未在前端流列表中${NC}"
    fi
    
    echo ""
    return 0
}


# 主函数
main() {
    if [ $# -lt 1 ]; then
        usage
    fi
    
    local command=$1
    shift
    
    case "$command" in
        create)
            if [ $# -lt 1 ]; then
                echo -e "${RED}❌ 错误: 需要提供协议参数${NC}"
                usage
            fi
            check_services
            
            # 创建流
            if ! create_stream "$@"; then
                exit 1
            fi
            
            # 验证流状态
            echo ""
            echo -e "${BLUE}══════════════════════════════════════════════════════${NC}"
            echo -e "${BLUE}验证流状态${NC}"
            echo -e "${BLUE}══════════════════════════════════════════════════════${NC}"
            echo ""
            
            if ! verify_stream "$CREATED_APP" "$CREATED_STREAM" "$CREATED_OUTPUT"; then
                echo -e "${YELLOW}⚠️  流验证失败，但流已创建${NC}"
            fi
            
            echo ""
            echo -e "${GREEN}══════════════════════════════════════════════════════${NC}"
            echo -e "${GREEN}流创建完成${NC}"
            echo -e "${GREEN}══════════════════════════════════════════════════════${NC}"
                echo ""
                echo -e "${GREEN}流信息:${NC}"
                echo "  App: $CREATED_APP"
                echo "  Stream: $CREATED_STREAM"
                echo "  删除命令: $0 delete $CREATED_APP $CREATED_STREAM"
            echo ""
            ;;
        delete)
            check_services
            delete_stream "$@"
            ;;
        list)
            check_services
            list_streams
            ;;
        cleanup)
            cleanup_resources
            ;;
        *)
            echo -e "${RED}❌ 未知命令: $command${NC}"
            usage
            ;;
    esac
}

main "$@"
