#!/bin/bash

# Centralized Dependency Installation Script
# Supports: Ubuntu/Debian, macOS

set -e

# Colors
GREEN='\033[0;32m'
RED='\033[0;31m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m'

echo -e "${BLUE}==========================================${NC}"
echo -e "${BLUE}Installing Dependencies${NC}"
echo -e "${BLUE}==========================================${NC}"

OS_TYPE=$(uname -s)
ARCH=$(uname -m)

echo "Detected OS: $OS_TYPE"
echo "Detected Arch: $ARCH"
echo ""

install_ubuntu() {
    echo -e "${YELLOW}Updates apt repository...${NC}"
    sudo apt-get update

    echo -e "${YELLOW}Installing packages...${NC}"
    sudo apt-get install -y \
        build-essential \
        g++ \
        pkg-config \
        cmake \
        git \
        curl \
        net-tools \
        iproute2 \
        openssl \
        ffmpeg \
        libavformat-dev \
        libavcodec-dev \
        libavutil-dev \
        libavfilter-dev \
        libswscale-dev \
        libcurl4-openssl-dev \
        libssl-dev \
        nlohmann-json3-dev \
        libspdlog-dev \
        libwebsocketpp-dev \
        libasio-dev \
        libcpp-httplib-dev

    echo -e "${GREEN}✓ Dependencies installed successfully (Ubuntu/Debian)${NC}"
}

install_macos() {
    if ! command -v brew &> /dev/null; then
        echo -e "${RED}Error: Homebrew not found. Please install Homebrew first.${NC}"
        exit 1
    fi

    echo -e "${YELLOW}Installing packages via Homebrew...${NC}"
    brew install cmake pkg-config ffmpeg curl openssl@3

    echo -e "${GREEN}✓ Dependencies installed successfully (macOS)${NC}"
}

if [[ "$OS_TYPE" == "Linux" ]]; then
    # Check for apt-get
    if command -v apt-get &> /dev/null; then
        install_ubuntu
    else
        echo -e "${RED}Error: Only apt-based Linux distributions (Ubuntu/Debian) are supported by this script.${NC}"
        echo "Please install dependencies manually."
        exit 1
    fi
elif [[ "$OS_TYPE" == "Darwin" ]]; then
    install_macos
else
    echo -e "${RED}Error: Unsupported OS: $OS_TYPE${NC}"
    exit 1
fi

echo ""
