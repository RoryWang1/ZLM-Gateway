#!/bin/bash
# 设置 msquic 库的脚本

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"
THIRD_PARTY_DIR="$PROJECT_ROOT/third_party"
MSQUIC_DIR="$THIRD_PARTY_DIR/msquic"

echo "=========================================="
echo "设置 msquic QUIC 库"
echo "=========================================="

# 检查 OpenSSL
if ! command -v openssl &> /dev/null; then
    echo "错误: 未找到 OpenSSL"
    echo "请安装 OpenSSL: brew install openssl@3"
    exit 1
fi

OPENSSL_VERSION=$(openssl version | awk '{print $2}')
echo "检测到 OpenSSL 版本: $OPENSSL_VERSION"

# 检查 CMake
if ! command -v cmake &> /dev/null; then
    echo "错误: 未找到 CMake"
    echo "请安装 CMake: brew install cmake"
    exit 1
fi

echo "检测到 CMake: $(cmake --version | head -n 1)"

# 创建 third_party 目录
mkdir -p "$THIRD_PARTY_DIR"
cd "$THIRD_PARTY_DIR"

# 克隆 msquic（如果不存在）
if [ ! -d "msquic" ]; then
    echo "正在克隆 msquic..."
    git clone https://github.com/microsoft/msquic.git
    cd msquic
    
    # 切换到稳定版本（推荐使用最新稳定版本）
    # v2.5.5 是 2024 年的稳定版本，支持 OpenSSL 3.x
    MSQUIC_VERSION="v2.5.5"
    echo "正在切换到稳定版本: $MSQUIC_VERSION"
    if git checkout "$MSQUIC_VERSION" 2>/dev/null; then
        echo "已切换到版本: $MSQUIC_VERSION"
    else
        echo "警告: 无法切换到 $MSQUIC_VERSION，使用最新 master 分支"
        echo "建议手动指定版本: git checkout v2.5.5"
    fi
else
    echo "msquic 目录已存在，跳过克隆"
    cd msquic
    
    # 检查当前版本
    CURRENT_VERSION=$(git describe --tags --exact-match 2>/dev/null || git rev-parse --short HEAD)
    echo "当前 msquic 版本: $CURRENT_VERSION"
    
    # 提示用户是否需要切换版本
    echo "如果需要切换到特定版本，请运行:"
    echo "  cd third_party/msquic"
    echo "  git checkout v2.5.5"
fi

# 更新子模块
echo "正在更新子模块..."
git submodule update --init --recursive

# 创建构建目录
BUILD_DIR="build"
mkdir -p "$BUILD_DIR"
cd "$BUILD_DIR"

# 检测 OpenSSL 路径
if [[ "$OSTYPE" == "darwin"* ]]; then
    # macOS: 检查 Homebrew OpenSSL
    OPENSSL_PREFIX=$(brew --prefix openssl@3 2>/dev/null || brew --prefix openssl 2>/dev/null || echo "")
    if [ -n "$OPENSSL_PREFIX" ]; then
        OPENSSL_ROOT_DIR="$OPENSSL_PREFIX"
        echo "使用 Homebrew OpenSSL: $OPENSSL_ROOT_DIR"
    else
        OPENSSL_ROOT_DIR="/usr/local/opt/openssl"
        echo "使用默认 OpenSSL 路径: $OPENSSL_ROOT_DIR"
    fi
else
    # Linux: 使用系统 OpenSSL
    OPENSSL_ROOT_DIR="/usr"
    echo "使用系统 OpenSSL: $OPENSSL_ROOT_DIR"
fi

# 检查 OpenSSL 头文件是否存在
if [ ! -f "$OPENSSL_ROOT_DIR/include/openssl/ssl.h" ]; then
    echo "警告: 在 $OPENSSL_ROOT_DIR/include/openssl/ssl.h 未找到 OpenSSL 头文件"
    echo "尝试查找系统 OpenSSL..."
    # 尝试其他常见路径
    for path in "/usr/local/opt/openssl@3" "/usr/local/opt/openssl" "/opt/homebrew/opt/openssl@3" "/opt/homebrew/opt/openssl"; do
        if [ -f "$path/include/openssl/ssl.h" ]; then
            OPENSSL_ROOT_DIR="$path"
            echo "找到 OpenSSL: $OPENSSL_ROOT_DIR"
            break
        fi
    done
fi

# 配置 CMake
echo "正在配置 CMake..."
echo "使用 OpenSSL 路径: $OPENSSL_ROOT_DIR"

cmake .. \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_INSTALL_PREFIX="$THIRD_PARTY_DIR/msquic/install" \
    -DQUIC_TLS_LIB=quictls \
    -DQUIC_USE_SYSTEM_LIBCRYPTO=OFF \
    -DOPENSSL_ROOT_DIR="$OPENSSL_ROOT_DIR" \
    -DQUIC_BUILD_SHARED=OFF \
    -DQUIC_BUILD_TOOLS=OFF \
    -DQUIC_BUILD_PERF=OFF \
    -DQUIC_BUILD_TEST=OFF

# 编译
echo "正在编译 msquic..."
cmake --build . --config Release -j$(sysctl -n hw.ncpu 2>/dev/null || nproc 2>/dev/null || echo 4)

# 安装（可选，用于开发）
echo "正在安装 msquic..."
cmake --install . --config Release

echo ""
echo "=========================================="
echo "msquic 设置完成！"
echo "=========================================="
echo ""
echo "库文件位置:"
echo "  - 静态库: $MSQUIC_DIR/$BUILD_DIR/bin/Release/libmsquic.a"
echo "  - 头文件: $MSQUIC_DIR/src/inc"
echo ""
echo "下一步:"
echo "  1. 更新 Makefile 以包含 msquic"
echo "  2. 更新 quic_server.cpp 以使用 msquic API"
echo ""

