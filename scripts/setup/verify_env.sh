#!/bin/bash

# Environment Verification Script
# Checks dependencies, configuration, and build status.

set -e

# Colors
GREEN='\033[0;32m'
RED='\033[0;31m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m'

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"
cd "$PROJECT_ROOT"

echo -e "${BLUE}==========================================${NC}"
echo -e "${BLUE}Verifying Environment${NC}"
echo -e "${BLUE}==========================================${NC}"
echo ""

ERRORS=0

# 1. System Dependencies
echo -e "${BLUE}Checking System Dependencies...${NC}"

check_cmd() {
    if command -v "$1" >/dev/null 2>&1; then
        echo -e "  [${GREEN}OK${NC}] $1: $(command -v $1)"
    else
        echo -e "  [${RED}FAIL${NC}] $1 not found"
        ERRORS=$((ERRORS + 1))
    fi
}

check_cmd "make"
check_cmd "g++"
check_cmd "cmake"
check_cmd "pkg-config"
check_cmd "curl"
check_cmd "python3"
check_cmd "openssl"

# FFmpeg Check (Special handling for System vs Local)
# We prioritize system ffmpeg for linux-arm64
echo -n "  Checking ffmpeg: "
if command -v ffmpeg >/dev/null 2>&1; then
    echo -e "[${GREEN}OK${NC}] (System: $(ffmpeg -version | head -n 1 | cut -d ' ' -f 3))"
elif [ -f "third_party/ffmpeg/macos-arm64/ffmpeg" ]; then
    echo -e "[${GREEN}OK${NC}] (Local: macos-arm64)"
elif [ -f "third_party/ffmpeg/linux-x64/ffmpeg" ]; then
    echo -e "[${GREEN}OK${NC}] (Local: linux-x64)"
else
    echo -e "[${RED}FAIL${NC}] Not found (System or Local)"
    ERRORS=$((ERRORS + 1))
fi

echo ""

# 2. Configuration
echo -e "${BLUE}Checking Configuration...${NC}"
if [ -f "configs/config.json" ]; then
    echo -e "  [${GREEN}OK${NC}] configs/config.json found"
else
    echo -e "  [${RED}FAIL${NC}] configs/config.json missing"
    echo "         Run: cp configs/config.example.json configs/config.json"
    ERRORS=$((ERRORS + 1))
fi

if [ -f "configs/zlm_config.ini" ]; then
    echo -e "  [${GREEN}OK${NC}] configs/zlm_config.ini found"
else
    echo -e "  [${RED}FAIL${NC}] configs/zlm_config.ini missing"
    ERRORS=$((ERRORS + 1))
fi

echo ""

# 3. Build Status
echo -e "${BLUE}Checking Build artifacts...${NC}"
if [ -f "bin/gateway_manager" ]; then
    echo -e "  [${GREEN}OK${NC}] bin/gateway_manager found"
else
    echo -e "  [${YELLOW}WARN${NC}] bin/gateway_manager missing (Need to build? Run 'make')"
fi

echo ""

# Summary
echo -e "${BLUE}==========================================${NC}"
if [ $ERRORS -eq 0 ]; then
    echo -e "${GREEN}Environment verification passed!${NC}"
    exit 0
else
    echo -e "${RED}Environment verification failed with $ERRORS errors.${NC}"
    echo "Please fix the issues above."
    exit 1
fi
