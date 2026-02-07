#!/bin/bash

# ZLM Gateway 系统管理脚本
# 用法: ./scripts/manage.sh [start|stop] [--clean-streams]

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
cd "$PROJECT_ROOT"

# 清除代理设置，防止 localhost 连接问题 (重要!)
unset http_proxy
unset https_proxy
unset HTTP_PROXY
unset HTTPS_PROXY

# 默认配置
FRONTEND_PORT="${FRONTEND_PORT:-5173}"

# 从配置文件读取 Gateway/ZLM 配置，如果没有则使用默认值
if [ -f "configs/config.json" ]; then
    # Gateway HTTP 端口（用于 API）
    GATEWAY_HTTP_PORT=$(python3 -c "import json; f=open('configs/config.json'); d=json.load(f); print(d.get('gateway', {}).get('http_port', 8080))" 2>/dev/null || echo "8080")
    GATEWAY_URL="${GATEWAY_URL:-http://localhost:${GATEWAY_HTTP_PORT}}"

    # ZLM HTTP 端口（用于 ZLMediaKit API）
    ZLM_HTTP_PORT=$(python3 -c "import json; f=open('configs/config.json'); d=json.load(f); print(d.get('zlmediakit', {}).get('http_port', 80))" 2>/dev/null || echo "80")
    ZLM_URL="${ZLM_URL:-http://localhost:${ZLM_HTTP_PORT}}"
else
    # 回退到历史默认值
    GATEWAY_URL="${GATEWAY_URL:-http://localhost:8088}"
    ZLM_URL="${ZLM_URL:-http://localhost:80}"
fi

# 颜色定义
GREEN='\033[0;32m'
RED='\033[0;31m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m'

# 显示用法
usage() {
    cat << EOF
用法: $0 [command] [options]

命令:
  start                启动所有服务 (ZLM, Gateway, 前端)
  stop                 停止所有服务
  status               查看服务状态
  setup                安装项目依赖 (apt/brew)
  check                检查环境依赖和配置
  build                编译 Gateway 主程序
  build_zlm            下载并编译 ZLMediaKit

选项:
  --clean-streams      停止服务时，同时清理正在运行的流（仅用于stop命令）

示例:
  $0 start              # 启动所有服务
  $0 stop               # 停止所有服务
  $0 stop --clean-streams  # 停止所有服务并清理流
  $0 status             # 查看服务状态
  $0 setup              # 安装依赖
  $0 check              # 检查环境
  $0 build_zlm          # 编译 ZLMediaKit
  $0 build              # 编译 Gateway

EOF
    exit 1
}

# 检查服务是否运行
check_zlm() {
    if curl --noproxy "*" -s "${ZLM_URL}/index/api/getServerConfig" > /dev/null 2>&1; then
        return 0
    fi
    return 1
}

check_gateway() {
    if curl --noproxy "*" -s "${GATEWAY_URL}/health" > /dev/null 2>&1; then
        return 0
    fi
    return 1
}

check_frontend() {
    if curl --noproxy "*" -s "http://localhost:${FRONTEND_PORT}" > /dev/null 2>&1; then
        return 0
    fi
    return 1
}

# 获取进程PID
get_zlm_pid() {
    ps aux | grep -v grep | grep MediaServer | awk '{print $2}' | head -1
}

get_gateway_pid() {
    ps aux | grep -v grep | grep "gateway_manager" | awk '{print $2}' | head -1
}

get_frontend_pid() {
    lsof -ti:${FRONTEND_PORT} 2>/dev/null | head -1
}

# 启动 ZLM
start_zlm() {
    echo -e "${BLUE}=========================================="
    echo "启动 ZLMediaKit"
    echo -e "==========================================${NC}"
    
    if check_zlm; then
        echo -e "${GREEN}✓ ZLMediaKit 已在运行${NC}"
        return 0
    fi
    
    # 检查进程是否运行但API不可用
    local zlm_pid=$(get_zlm_pid)
    if [ -n "$zlm_pid" ]; then
        echo -e "${YELLOW}⚠️  检测到 ZLM 进程 (PID: $zlm_pid) 但 API 不可访问${NC}"
        echo "   等待服务启动..."
        for i in {1..10}; do
            sleep 1
            if check_zlm; then
                echo -e "${GREEN}✓ ZLMediaKit 现在可访问${NC}"
                return 0
            fi
        done
    fi
    
    # 启动 ZLM
    echo "正在启动 ZLMediaKit..."
    bash "$SCRIPT_DIR/setup/start_zlmediakit.sh"
    
    if check_zlm; then
        echo -e "${GREEN}✓ ZLMediaKit 启动成功${NC}"
    else
        echo -e "${RED}❌ ZLMediaKit 启动失败${NC}"
        return 1
    fi
    echo ""
}

# 启动 Gateway
start_gateway() {
    echo -e "${BLUE}=========================================="
    echo "启动 Gateway Manager"
    echo -e "==========================================${NC}"
    
    mkdir -p logs
    
    if check_gateway; then
        echo -e "${GREEN}✓ Gateway Manager 已在运行${NC}"
        return 0
    fi
    
    # 检查二进制文件是否存在
    if [ ! -f "./bin/gateway_manager" ]; then
        echo -e "${RED}❌ Gateway 二进制文件不存在: ./bin/gateway_manager${NC}"
        echo "   请先编译项目: make release"
        return 1
    fi
    
    echo "正在启动 Gateway Manager..."
    ./bin/gateway_manager > logs/gateway_manager.log 2>&1 &
    local gateway_pid=$!
    
    echo "等待 Gateway 启动..."
    for i in {1..10}; do
        sleep 1
        if check_gateway; then
            echo -e "${GREEN}✓ Gateway Manager 启动成功 (PID: $gateway_pid)${NC}"
            echo ""
            return 0
        fi
        echo -n "."
    done
    
    echo ""
    echo -e "${RED}❌ Gateway Manager 启动失败${NC}"
    echo "   请检查日志: logs/gateway_manager.log"
    return 1
}

# 启动前端
start_frontend() {
    echo -e "${BLUE}=========================================="
    echo "启动前端开发服务器"
    echo -e "==========================================${NC}"
    
    if check_frontend; then
        echo -e "${GREEN}✓ 前端服务器已在运行${NC}"
        return 0
    fi
    
    # 检查前端目录
    if [ ! -d "frontend" ]; then
        echo -e "${RED}❌ 前端目录不存在: frontend${NC}"
        return 1
    fi
    
    # 检查 node_modules
    if [ ! -d "frontend/node_modules" ]; then
        echo -e "${YELLOW}⚠️  前端依赖未安装，正在安装...${NC}"
        cd frontend
        npm install
        cd "$PROJECT_ROOT"
    fi
    
    echo "正在启动前端..."
    cd frontend
    npm run dev > "$PROJECT_ROOT/logs/frontend.log" 2>&1 &
    local frontend_pid=$!
    cd "$PROJECT_ROOT"
    
    echo "等待前端服务器启动..."
    for i in {1..20}; do
        sleep 1
        if check_frontend; then
            echo -e "${GREEN}✓ 前端服务器启动成功 (PID: $frontend_pid)${NC}"
            echo -e "${GREEN}  访问: http://localhost:${FRONTEND_PORT}${NC}"
            echo ""
            return 0
        fi
        echo -n "."
    done
    
    echo ""
    echo -e "${RED}❌ 前端服务器启动失败${NC}"
    echo "   请检查日志: logs/frontend.log"
    return 1
}

# 停止 ZLM
stop_zlm() {
    echo -e "${BLUE}=========================================="
    echo "停止 ZLMediaKit"
    echo -e "==========================================${NC}"
    
    local zlm_pid=$(get_zlm_pid)
    if [ -z "$zlm_pid" ]; then
        echo -e "${GREEN}✓ ZLMediaKit 未运行${NC}"
        return 0
    fi
    
    echo "正在停止 ZLMediaKit (PID: $zlm_pid)..."
    kill "$zlm_pid" 2>/dev/null || true
    
    # 等待进程退出
    for i in {1..10}; do
        if ! ps -p "$zlm_pid" > /dev/null 2>&1; then
            echo -e "${GREEN}✓ ZLMediaKit 已停止${NC}"
            echo ""
            return 0
        fi
        sleep 1
    done
    
    # 强制杀死
    echo -e "${YELLOW}⚠️  进程未响应，强制停止...${NC}"
    kill -9 "$zlm_pid" 2>/dev/null || true
    sleep 1
    
    if ! ps -p "$zlm_pid" > /dev/null 2>&1; then
        echo -e "${GREEN}✓ ZLMediaKit 已强制停止${NC}"
    else
        echo -e "${RED}❌ 无法停止 ZLMediaKit${NC}"
        return 1
    fi
    echo ""
}

# 停止 Gateway
stop_gateway() {
    echo -e "${BLUE}=========================================="
    echo "停止 Gateway Manager"
    echo -e "==========================================${NC}"
    
    local gateway_pid=$(get_gateway_pid)
    if [ -z "$gateway_pid" ]; then
        echo -e "${GREEN}✓ Gateway Manager 未运行${NC}"
        return 0
    fi
    
    echo "正在停止 Gateway Manager (PID: $gateway_pid)..."
    kill "$gateway_pid" 2>/dev/null || true
    
    # 等待进程退出
    for i in {1..10}; do
        if ! ps -p "$gateway_pid" > /dev/null 2>&1; then
            echo -e "${GREEN}✓ Gateway Manager 已停止${NC}"
            echo ""
            return 0
        fi
        sleep 1
    done
    
    # 强制杀死
    echo -e "${YELLOW}⚠️  进程未响应，强制停止...${NC}"
    kill -9 "$gateway_pid" 2>/dev/null || true
    sleep 1
    
    if ! ps -p "$gateway_pid" > /dev/null 2>&1; then
        echo -e "${GREEN}✓ Gateway Manager 已强制停止${NC}"
    else
        echo -e "${RED}❌ 无法停止 Gateway Manager${NC}"
        return 1
    fi
    echo ""
}

# 停止前端
stop_frontend() {
    echo -e "${BLUE}=========================================="
    echo "停止前端开发服务器"
    echo -e "==========================================${NC}"
    
    local frontend_pid=$(get_frontend_pid)
    if [ -z "$frontend_pid" ]; then
        echo -e "${GREEN}✓ 前端服务器未运行${NC}"
        return 0
    fi
    
    echo "正在停止前端服务器 (PID: $frontend_pid)..."
    kill "$frontend_pid" 2>/dev/null || true
    
    # 等待进程退出
    for i in {1..10}; do
        if ! ps -p "$frontend_pid" > /dev/null 2>&1; then
            echo -e "${GREEN}✓ 前端服务器已停止${NC}"
            echo ""
            return 0
        fi
        sleep 1
    done
    
    # 强制杀死
    echo -e "${YELLOW}⚠️  进程未响应，强制停止...${NC}"
    kill -9 "$frontend_pid" 2>/dev/null || true
    sleep 1
    
    if ! ps -p "$frontend_pid" > /dev/null 2>&1; then
        echo -e "${GREEN}✓ 前端服务器已强制停止${NC}"
    else
        echo -e "${RED}❌ 无法停止前端服务器${NC}"
        return 1
    fi
    echo ""
}

# 检查并清理流
cleanup_streams() {
    echo -e "${BLUE}=========================================="
    echo "检查并清理正在运行的流"
    echo -e "==========================================${NC}"
    
    if ! check_gateway; then
        echo -e "${YELLOW}⚠️  Gateway API 不可用，跳过流清理${NC}"
        return 0
    fi
    
    local response=$(curl -s "${GATEWAY_URL}/api/v1/streams" 2>/dev/null)
    if [ -z "$response" ]; then
        echo -e "${YELLOW}⚠️  无法获取流列表${NC}"
        return 0
    fi
    
    # 使用 Python 解析 JSON 并查找运行中的流
    local running_streams=$(echo "$response" | python3 -c "
import sys, json
try:
    data = json.load(sys.stdin)
    streams = data.get('data', [])
    running = [s for s in streams if s.get('status') == 2 or s.get('status_text') == 'running']
    for s in running:
        app = s.get('app', 'live')
        stream = s.get('stream', '')
        protocol = s.get('protocol', 'unknown')
        print(f'{app}/{stream} ({protocol})')
    print(f'__COUNT__:{len(running)}')
except Exception as e:
    print('__ERROR__:' + str(e))
" 2>/dev/null)
    
    if echo "$running_streams" | grep -q "__ERROR__"; then
        echo -e "${YELLOW}⚠️  解析流列表时出错${NC}"
        return 0
    fi
    
    local count=$(echo "$running_streams" | grep "__COUNT__" | cut -d: -f2)
    if [ -z "$count" ] || [ "$count" -eq 0 ]; then
        echo -e "${GREEN}✓ 没有正在运行的流${NC}"
        echo ""
        return 0
    fi
    
    echo -e "${YELLOW}发现 $count 个正在运行的流:${NC}"
    echo "$running_streams" | grep -v "__COUNT__" | while read -r stream_info; do
        echo "  - $stream_info"
    done
    echo ""
    
    # 删除所有运行中的流（使用临时文件避免子shell问题）
    local temp_file=$(mktemp)
    echo "$running_streams" | grep -v "__COUNT__" > "$temp_file"
    
    local deleted=0
    while IFS= read -r stream_info; do
        local app=$(echo "$stream_info" | cut -d'/' -f1)
        local stream=$(echo "$stream_info" | cut -d'/' -f2 | cut -d' ' -f1)
        
        if [ -n "$app" ] && [ -n "$stream" ]; then
            local delete_response=$(curl -s -X DELETE "${GATEWAY_URL}/api/v1/streams/${app}/${stream}" 2>/dev/null)
            if echo "$delete_response" | python3 -c "import sys, json; data=json.load(sys.stdin); sys.exit(0 if data.get('code') == 0 else 1)" 2>/dev/null; then
                echo -e "${GREEN}  ✓ 已删除: ${app}/${stream}${NC}"
                deleted=$((deleted + 1))
            else
                echo -e "${YELLOW}  ⚠️  删除失败: ${app}/${stream}${NC}"
            fi
        fi
    done < "$temp_file"
    rm -f "$temp_file"
    
    echo ""
    if [ "$deleted" -gt 0 ]; then
        echo -e "${GREEN}✓ 流清理完成，已删除 $deleted 个流${NC}"
    else
        echo -e "${GREEN}✓ 流清理完成${NC}"
    fi
    echo ""
}

# 显示服务状态
show_status() {
    echo -e "${BLUE}══════════════════════════════════════════════════════${NC}"
    echo -e "${BLUE}服务状态${NC}"
    echo -e "${BLUE}══════════════════════════════════════════════════════${NC}"
    echo ""
    
    # ZLM 状态
    local zlm_pid=$(get_zlm_pid)
    if [ -n "$zlm_pid" ] && check_zlm; then
        echo -e "${GREEN}✓ ZLMediaKit${NC}    运行中 (PID: $zlm_pid)"
        echo "   API: ${ZLM_URL}"
    elif [ -n "$zlm_pid" ]; then
        echo -e "${YELLOW}⚠ ZLMediaKit${NC}    进程存在但API不可访问 (PID: $zlm_pid)"
    else
        echo -e "${RED}✗ ZLMediaKit${NC}    未运行"
    fi
    echo ""
    
    # Gateway 状态
    local gateway_pid=$(get_gateway_pid)
    if [ -n "$gateway_pid" ] && check_gateway; then
        echo -e "${GREEN}✓ Gateway Manager${NC} 运行中 (PID: $gateway_pid)"
        echo "   API: ${GATEWAY_URL}"
    elif [ -n "$gateway_pid" ]; then
        echo -e "${YELLOW}⚠ Gateway Manager${NC} 进程存在但API不可访问 (PID: $gateway_pid)"
    else
        echo -e "${RED}✗ Gateway Manager${NC} 未运行"
    fi
    echo ""
    
    # 前端状态
    local frontend_pid=$(get_frontend_pid)
    if [ -n "$frontend_pid" ] && check_frontend; then
        echo -e "${GREEN}✓ 前端服务器${NC}    运行中 (PID: $frontend_pid)"
        echo "   URL: http://localhost:${FRONTEND_PORT}"
    elif [ -n "$frontend_pid" ]; then
        echo -e "${YELLOW}⚠ 前端服务器${NC}    进程存在但服务不可访问 (PID: $frontend_pid)"
    else
        echo -e "${RED}✗ 前端服务器${NC}    未运行"
    fi
    echo ""
    
    # 流状态（如果Gateway可用）
    if check_gateway; then
        local response=$(curl -s "${GATEWAY_URL}/api/v1/streams" 2>/dev/null)
        if [ -n "$response" ]; then
            local total=$(echo "$response" | python3 -c "import sys, json; data=json.load(sys.stdin); print(len(data.get('data', [])))" 2>/dev/null || echo "0")
            local running=$(echo "$response" | python3 -c "import sys, json; data=json.load(sys.stdin); streams=data.get('data', []); print(len([s for s in streams if s.get('status') == 2]))" 2>/dev/null || echo "0")
            
            if [ "$total" -gt 0 ]; then
                echo -e "${BLUE}流状态:${NC} 总计 $total 个流，其中 $running 个正在运行"
            else
                echo -e "${BLUE}流状态:${NC} 无流"
            fi
        fi
    fi
    
    echo ""
    echo -e "${BLUE}══════════════════════════════════════════════════════${NC}"
}

# 主函数
main() {
    if [ $# -eq 0 ]; then
        usage
    fi
    
    local command="$1"
    shift
    
    case "$command" in
        start)
            echo -e "${BLUE}══════════════════════════════════════════════════════${NC}"
            echo -e "${BLUE}启动所有服务${NC}"
            echo -e "${BLUE}══════════════════════════════════════════════════════${NC}"
            echo ""
            
            start_zlm || exit 1
            start_gateway || exit 1
            start_frontend || exit 1
            
            echo -e "${GREEN}══════════════════════════════════════════════════════${NC}"
            echo -e "${GREEN}所有服务启动完成${NC}"
            echo -e "${GREEN}══════════════════════════════════════════════════════${NC}"
            echo ""
            echo "服务地址:"
            echo "  - 前端: http://localhost:${FRONTEND_PORT}"
            echo "  - Gateway API: ${GATEWAY_URL}"
            echo "  - ZLM API: ${ZLM_URL}"
            ;;
        
        stop)
            local clean_streams=false
            while [ $# -gt 0 ]; do
                case "$1" in
                    --clean-streams)
                        clean_streams=true
                        shift
                        ;;
                    *)
                        echo -e "${RED}❌ 未知参数: $1${NC}"
                        usage
                        ;;
                esac
            done
            
            echo -e "${BLUE}══════════════════════════════════════════════════════${NC}"
            echo -e "${BLUE}停止所有服务${NC}"
            echo -e "${BLUE}══════════════════════════════════════════════════════${NC}"
            echo ""
            
            if [ "$clean_streams" = true ]; then
                cleanup_streams
            fi
            
            stop_frontend
            stop_gateway
            stop_zlm
            
            echo -e "${GREEN}══════════════════════════════════════════════════════${NC}"
            echo -e "${GREEN}所有服务已停止${NC}"
            echo -e "${GREEN}══════════════════════════════════════════════════════${NC}"
            ;;
        
        status)
            show_status
            ;;
        
        setup)
            bash "$SCRIPT_DIR/setup/install_deps.sh"
            ;;
        
        check)
            bash "$SCRIPT_DIR/setup/verify_env.sh"
            ;;

        build)
            echo "Building Gateway..."
            make
            ;;

        build_zlm)
            bash "$SCRIPT_DIR/setup/setup_zlmediakit.sh"
            ;;
        
        *)
            echo -e "${RED}❌ 未知命令: $command${NC}"
            usage
            ;;
    esac
}

main "$@"

