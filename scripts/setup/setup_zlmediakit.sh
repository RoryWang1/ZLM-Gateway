#!/bin/bash

# ZLMediaKit Setup Script for Linux ARM64 (and others)
# Clones and builds ZLMediaKit using system dependencies where possible.

set -e

# Colors
GREEN='\033[0;32m'
RED='\033[0;31m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m'

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"
THIRD_PARTY_DIR="$PROJECT_ROOT/third_party"
ZLM_DIR="$THIRD_PARTY_DIR/zlmediakit"

echo -e "${BLUE}==========================================${NC}"
echo -e "${BLUE}Setting up ZLMediaKit${NC}"
echo -e "${BLUE}==========================================${NC}"

# 1. Prepare Directory
mkdir -p "$THIRD_PARTY_DIR"
cd "$THIRD_PARTY_DIR"

# 2. Clone ZLMediaKit
if [ ! -d "zlmediakit" ]; then
    echo -e "${YELLOW}Cloning ZLMediaKit...${NC}"
    # Using a specific stable tag or commit is recommended for stability, 
    # but for now we clone master as per common practice, or we could pick a known good tag.
    # Let's use master for latest fixes on ARM.
    git clone --depth 1 https://github.com/xia-chu/ZLMediaKit.git zlmediakit
    cd zlmediakit
    echo -e "${YELLOW}Updating submodules...${NC}"
    git submodule update --init
else
    echo -e "${GREEN}ZLMediaKit already exists.${NC}"
    cd zlmediakit
    # Optional: git pull
fi

# 3. Build ZLMediaKit
echo -e "${YELLOW}Building ZLMediaKit...${NC}"
mkdir -p build
cd build

# CMake Options
# -DENABLE_OPENSSL=ON: Enable OpenSSL
# -DENABLE_WEBRTC=ON: Enable WebRTC (usually desired)
# -DENABLE_TESTS=OFF: Skip tests to save time
# -DENABLE_API=ON: Enable Http API
# -DENABLE_CXXAPI=ON: Enable C++ API (needed for Gateway linkage)
# -DOPENSSL_ROOT_DIR=/usr: Explicitly tell it to look in system paths if needed, 
#                          though standard cmake lookup usually finds system openssl on linux.

CMAKE_OPTIONS="-DENABLE_OPENSSL=ON -DENABLE_WEBRTC=ON -DENABLE_TESTS=OFF -DENABLE_API=ON -DENABLE_CXXAPI=ON"

if [[ "$(uname -s)" == "Linux" ]]; then
    # Ensure it finds system OpenSSL on Linux
    CMAKE_OPTIONS="$CMAKE_OPTIONS -DOPENSSL_USE_STATIC_LIBS=OFF" 
fi

echo "CMake Options: $CMAKE_OPTIONS"

cmake .. $CMAKE_OPTIONS

# Compile
CORES=$(nproc 2>/dev/null || sysctl -n hw.ncpu 2>/dev/null || echo 4)
make -j$CORES

echo -e "${GREEN}✓ ZLMediaKit built successfully.${NC}"

# 4. Verify Output
if [ -f "../release/linux/Release/MediaServer" ] || [ -f "../release/mac/Release/MediaServer" ]; then
     echo -e "${GREEN}MediaServer binary found.${NC}"
else
     echo -e "${RED}Warning: MediaServer binary not found in expected standard paths.${NC}"
fi
