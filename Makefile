# ZLMediaKit Gateway - Makefile
# C++17项目构建文件

# 编译器设置
CXX = g++
CXXFLAGS = -std=c++17 -Wall -Wextra -O2 -g -DASIO_STANDALONE -Wno-null-pointer-subtraction
CXXFLAGS_DEBUG = -std=c++17 -Wall -Wextra -g -O0 -DDEBUG
CXXFLAGS_RELEASE = -std=c++17 -Wall -Wextra -O3 -DNDEBUG

# 目录设置
SRC_DIR = src
INCLUDE_DIR = include
THIRD_PARTY_DIR = third_party
BUILD_DIR = build
BIN_DIR = bin
OBJ_DIR = $(BUILD_DIR)/obj

# 检测操作系统架构
UNAME_M := $(shell uname -m)
UNAME_S := $(shell uname -s)

# FFmpeg 二进制路径（项目内）
ifeq ($(UNAME_S),Darwin)
    ifeq ($(UNAME_M),arm64)
        FFMPEG_PLATFORM = macos-arm64
    else
        FFMPEG_PLATFORM = macos-x86_64
    endif

else
    # Linux (Generic)
    FFMPEG_PLATFORM = linux
    # Linux 推荐完全使用系统依赖
    USE_SYSTEM_DEPS = 1
endif

# 如果设置了 USE_SYSTEM_DEPS，打印提示
ifeq ($(USE_SYSTEM_DEPS),1)
    $(info 构建模式: 强制使用系统依赖 (USE_SYSTEM_DEPS=1))
endif

FFMPEG_BIN_DIR = $(THIRD_PARTY_DIR)/ffmpeg/$(FFMPEG_PLATFORM)

ifeq ($(USE_SYSTEM_DEPS),1)
    # 系统依赖模式：直接使用系统命令
    FFMPEG_BINARY = ffmpeg
    FFPROBE_BINARY = ffprobe
else
    # 混合模式：优先使用项目内二进制
    FFMPEG_BINARY = $(FFMPEG_BIN_DIR)/ffmpeg
    FFPROBE_BINARY = $(FFMPEG_BIN_DIR)/ffprobe
endif

# CURL 库路径（项目内）
CURL_DIR = $(THIRD_PARTY_DIR)/curl
CURL_PLATFORM_DIR = $(CURL_DIR)/$(FFMPEG_PLATFORM)

ifneq ($(USE_SYSTEM_DEPS),1)
    CURL_LIB_DIR = $(CURL_PLATFORM_DIR)/lib
    CURL_INCLUDE_DIR = $(CURL_PLATFORM_DIR)/include
endif

# msquic 库路径（QUIC 实现）
MSQUIC_DIR = $(THIRD_PARTY_DIR)/msquic
MSQUIC_BUILD_DIR = $(MSQUIC_DIR)/build
MSQUIC_INCLUDE_DIR = $(MSQUIC_DIR)/src/inc
MSQUIC_LIB = $(MSQUIC_BUILD_DIR)/bin/Release/libmsquic.a

# 检查 msquic 是否存在
MSQUIC_AVAILABLE = $(shell test -f $(MSQUIC_LIB) && echo "yes" || echo "no")

# OpenSSL 路径
OPENSSL_PREFIX = $(shell brew --prefix openssl@3 2>/dev/null || brew --prefix openssl 2>/dev/null || echo "")

# 包含目录
# 包含目录
ifeq ($(USE_SYSTEM_DEPS),1)
    # 系统依赖模式：不包含 third_party 下的 json/spdlog/etc，使用系统路径
    INCLUDES = -I$(INCLUDE_DIR) \
               -I$(SRC_DIR) \
               -I$(THIRD_PARTY_DIR) \
               -I$(CURL_INCLUDE_DIR)
else
    INCLUDES = -I$(INCLUDE_DIR) \
               -I$(SRC_DIR) \
               -I$(THIRD_PARTY_DIR) \
               -I$(THIRD_PARTY_DIR)/httplib \
               -I$(THIRD_PARTY_DIR)/json/include \
               -I$(THIRD_PARTY_DIR)/spdlog/include \
               -I$(THIRD_PARTY_DIR)/websocketpp \
               -I$(THIRD_PARTY_DIR)/asio/asio/include \
               -I$(CURL_INCLUDE_DIR)
endif

# 如果 OpenSSL 可用，添加包含目录
ifneq ($(OPENSSL_PREFIX),)
    INCLUDES += -I$(OPENSSL_PREFIX)/include
endif

# 如果 msquic 可用，添加包含目录
ifneq ($(MSQUIC_AVAILABLE),no)
    INCLUDES += -I$(MSQUIC_INCLUDE_DIR)
    CXXFLAGS += -DHAVE_MSQUIC
    $(info 检测到 msquic，启用 QUIC 支持)
else
    $(info 警告: msquic 未找到，QUIC 服务器模式将不可用)
    $(info 运行 scripts/setup/setup_msquic.sh 来安装 msquic)
endif

# 库目录
LIB_DIRS = -L/usr/local/lib \
           -L$(THIRD_PARTY_DIR)/zlmediakit/lib

# 非系统依赖模式下才添加本地 CURL lib 路径
ifneq ($(USE_SYSTEM_DEPS),1)
    LIB_DIRS += -L$(CURL_LIB_DIR)
endif

# 如果 msquic 可用，添加库目录
ifneq ($(MSQUIC_AVAILABLE),no)
    LIB_DIRS += -L$(MSQUIC_BUILD_DIR)/bin/Release
endif

# 使用pkg-config获取依赖库
PKG_CONFIG = pkg-config

# FFmpeg库
FFMPEG_CFLAGS = $(shell $(PKG_CONFIG) --cflags libavformat libavcodec libavutil libavfilter libswscale 2>/dev/null || echo "")
FFMPEG_LIBS = $(shell $(PKG_CONFIG) --libs libavformat libavcodec libavutil libavfilter libswscale 2>/dev/null || echo "")

# libcurl Logic
ifeq ($(USE_SYSTEM_DEPS),1)
    # 强制使用系统 CURL
    CURL_CFLAGS = $(shell $(PKG_CONFIG) --cflags libcurl 2>/dev/null || echo "")
    CURL_LIBS = $(shell $(PKG_CONFIG) --libs libcurl 2>/dev/null || echo "-lcurl")
    $(info 使用系统 CURL (USE_SYSTEM_DEPS=1))
else
    # 原有逻辑：优先检查本地，回退系统
    # 优先使用项目内的 CURL（如果存在），否则使用系统 CURL
    # ... (原有查找逻辑) ...
    CURL_STATIC_LIB = $(shell find $(CURL_LIB_DIR) -name "libcurl.a" 2>/dev/null | head -1)
    CURL_DYNAMIC_LIB = $(shell find $(CURL_LIB_DIR) -name "libcurl*.dylib" -o -name "libcurl*.so" 2>/dev/null | head -1)
    CURL_TBD_LIB = $(shell find $(CURL_LIB_DIR) -name "libcurl*.tbd" 2>/dev/null | head -1)
    CURL_LIB_FILE = $(if $(CURL_STATIC_LIB),$(CURL_STATIC_LIB),$(if $(CURL_DYNAMIC_LIB),$(CURL_DYNAMIC_LIB),$(CURL_TBD_LIB)))

    ifneq ($(CURL_LIB_FILE),)
        # 使用项目内的 CURL
        CURL_CFLAGS = -I$(CURL_INCLUDE_DIR)
        ifneq ($(CURL_STATIC_LIB),)
            # 使用静态库
            CURL_LIBS = $(CURL_STATIC_LIB)
            $(info 使用项目内 CURL 静态库: $(CURL_STATIC_LIB))
        else ifneq ($(CURL_DYNAMIC_LIB),)
            # 使用动态库
            CURL_LIBS = -L$(CURL_LIB_DIR) -lcurl
            $(info 使用项目内 CURL 动态库: $(CURL_DYNAMIC_LIB))
        else ifneq ($(CURL_TBD_LIB),)
            # 使用 .tbd 文件
            CURL_LIBS = -L$(CURL_LIB_DIR) -lcurl
            $(info 使用项目内 CURL .tbd 文件: $(CURL_TBD_LIB))
        endif
    else
        # 回退到系统 CURL
        CURL_CFLAGS = $(shell $(PKG_CONFIG) --cflags libcurl 2>/dev/null || echo "")
        CURL_LIBS = $(shell $(PKG_CONFIG) --libs libcurl 2>/dev/null || echo "-lcurl")
        $(info 使用系统 CURL (项目内 CURL 不存在: $(CURL_LIB_DIR)))
    endif
endif



# 编译选项
CXXFLAGS += $(INCLUDES) $(FFMPEG_CFLAGS) $(CURL_CFLAGS) -DCPPHTTPLIB_OPENSSL_SUPPORT

# 链接库
# CURL 静态库需要额外的依赖库
# 检查 Homebrew 的 nghttp2 和 libidn2（如果存在）
# 检查是否使用 OpenSSL（如果使用 OpenSSL 编译的 curl，需要链接 OpenSSL）
HOMEBREW_NGHTTP2 = $(shell find /opt/homebrew/lib -name "libnghttp2*.dylib" -o -name "libnghttp2*.a" 2>/dev/null | head -1)
HOMEBREW_LIBIDN2 = $(shell find /opt/homebrew/lib -name "libidn2*.dylib" -o -name "libidn2*.a" 2>/dev/null | head -1)
OPENSSL_PREFIX = $(shell brew --prefix openssl@3 2>/dev/null || brew --prefix openssl 2>/dev/null || echo "")
CURL_EXTRA_LIBS = -lz
# 检查 curl 是否使用 OpenSSL（通过检查 libcurl.a 的符号）
CURL_USES_OPENSSL = $(shell nm $(CURL_STATIC_LIB) 2>/dev/null | grep -q "SSL_" && echo "yes" || echo "no")
ifeq ($(CURL_USES_OPENSSL),yes)
    ifneq ($(OPENSSL_PREFIX),)
        CURL_EXTRA_LIBS += -L$(OPENSSL_PREFIX)/lib -lssl -lcrypto
        $(info 检测到 CURL 使用 OpenSSL，添加 OpenSSL 库: $(OPENSSL_PREFIX)/lib)
    else
        CURL_EXTRA_LIBS += -lssl -lcrypto
        $(info 检测到 CURL 使用 OpenSSL，使用系统 OpenSSL)
    endif
    # macOS 特定的功能（如代理检测）仍需要 CoreFoundation
    CURL_EXTRA_LIBS += -framework CoreFoundation -framework SystemConfiguration
else
    # 使用 Secure Transport
    CURL_EXTRA_LIBS += -framework Security -framework CoreFoundation -framework SystemConfiguration
    $(info CURL 使用 Secure Transport)
endif
ifneq ($(HOMEBREW_NGHTTP2),)
    CURL_EXTRA_LIBS += -L/opt/homebrew/lib -lnghttp2
    $(info 使用 Homebrew nghttp2: $(HOMEBREW_NGHTTP2))
endif
ifneq ($(HOMEBREW_LIBIDN2),)
    CURL_EXTRA_LIBS += -L/opt/homebrew/lib -lidn2
    $(info 使用 Homebrew libidn2: $(HOMEBREW_LIBIDN2))
endif
# msquic 库链接（如果可用）
MSQUIC_LIBS = 
ifneq ($(MSQUIC_AVAILABLE),no)
    MSQUIC_LIBS = $(MSQUIC_LIB)
    
    
    # 在系统依赖模式下，msquic 使用 bundled quictls (静态)
    MSQUIC_QUICTLS_LIB = $(MSQUIC_BUILD_DIR)/_deps/opensslquic-build/quictls/lib
    MSQUIC_LIBS += $(MSQUIC_QUICTLS_LIB)/libssl.a $(MSQUIC_QUICTLS_LIB)/libcrypto.a
    
    # msquic 需要额外的系统库（macOS）
    ifeq ($(UNAME_S),Darwin)
        MSQUIC_LIBS += -framework CoreFoundation -framework Security
    endif
endif

LIBS = $(FFMPEG_LIBS) \
       $(CURL_LIBS) \
       $(if $(CURL_STATIC_LIB),$(CURL_EXTRA_LIBS),) \
       $(MSQUIC_LIBS) \
       -lssl -lcrypto \
       -lz -lfmt -lcpp-httplib \
       -lpthread \
       -ldl

# 源文件
CONFIG_SRC = $(SRC_DIR)/config/config_loader.cpp
UTILS_SRC = $(SRC_DIR)/utils/logger.cpp \
	$(SRC_DIR)/utils/zlm_stream_checker.cpp \
	$(SRC_DIR)/utils/ffmpeg_log_analyzer.cpp \
            $(SRC_DIR)/utils/bitrate_allocator.cpp \
            $(SRC_DIR)/utils/camera_detector.cpp \
            $(SRC_DIR)/utils/stream_status_checker.cpp \
            $(SRC_DIR)/utils/http_callback.cpp \
            $(SRC_DIR)/utils/zlm_url_builder.cpp \
            $(SRC_DIR)/utils/url_parser.cpp \
            $(SRC_DIR)/utils/audio_codec_detector.cpp \
            $(SRC_DIR)/utils/ffmpeg_params.cpp \
            $(SRC_DIR)/utils/system_utils.cpp
# Streaming 源文件
STREAMING_SRC = $(SRC_DIR)/streaming/zlmediakit/zlm_client.cpp \
		$(SRC_DIR)/streaming/stream_manager.cpp \
		$(SRC_DIR)/streaming/stream_start_queue.cpp
PROCESS_SRC = $(SRC_DIR)/process/process_manager.cpp \
              $(SRC_DIR)/process/process_monitor.cpp \
              $(SRC_DIR)/process/ffmpeg_executor.cpp \
              $(SRC_DIR)/process/ffprobe_detector.cpp
MONITORING_SRC = $(SRC_DIR)/monitoring/statistics_manager.cpp \
                 $(SRC_DIR)/monitoring/alert_manager.cpp \
                 $(SRC_DIR)/monitoring/webhook_notifier.cpp
API_SRC = $(SRC_DIR)/api/http_server.cpp \
          $(SRC_DIR)/api/websocket_server.cpp \
          $(SRC_DIR)/api/utils/response_helper.cpp \
          $(SRC_DIR)/api/handlers/stream_handler.cpp \
          $(SRC_DIR)/api/handlers/device_handler.cpp \
          $(SRC_DIR)/api/handlers/statistics_handler.cpp \
          $(SRC_DIR)/api/handlers/alert_handler.cpp \
          $(SRC_DIR)/api/handlers/hook_handler.cpp \
          $(SRC_DIR)/api/handlers/process_handler.cpp
GATEWAY_RTSP_SRC = $(SRC_DIR)/gateway/rtsp/rtsp_gateway.cpp
GATEWAY_HTTPFLV_SRC = $(SRC_DIR)/gateway/httpflv/httpflv_gateway.cpp
GATEWAY_DASH_SRC = $(SRC_DIR)/gateway/dash/dash_gateway.cpp
GATEWAY_HLS_SRC = $(SRC_DIR)/gateway/hls/hls_gateway.cpp
GATEWAY_QUIC_SRC = $(SRC_DIR)/gateway/quic/quic_gateway.cpp \
                   $(SRC_DIR)/gateway/quic/quic_server.cpp \
                   $(SRC_DIR)/gateway/quic/fec_processor.cpp \
                   $(SRC_DIR)/gateway/quic/ts_parser.cpp \
                   $(SRC_DIR)/gateway/quic/ts_to_ffmpeg_bridge.cpp
GATEWAY_ONVIF_SRC = $(SRC_DIR)/gateway/onvif/onvif_gateway.cpp
GATEWAY_RTMP_SRC = $(SRC_DIR)/gateway/rtmp/rtmp_gateway.cpp
GATEWAY_ISAPI_SRC = $(SRC_DIR)/gateway/isapi/isapi_gateway.cpp
GATEWAY_DAHUA_SRC = $(SRC_DIR)/gateway/dahua/dahua_gateway.cpp
GATEWAY_PSIA_SRC = $(SRC_DIR)/gateway/psia/psia_gateway.cpp
GATEWAY_LOCAL_CAMERA_SRC = $(SRC_DIR)/gateway/local_camera/local_camera_gateway.cpp \
                           $(SRC_DIR)/gateway/local_camera/device_manager.cpp \
                           $(SRC_DIR)/gateway/local_camera/device_resolver.cpp \
                           $(SRC_DIR)/gateway/local_camera/stream_validator.cpp \
                           $(SRC_DIR)/gateway/local_camera/health_monitor.cpp

GATEWAY_UTILS_SRC = $(SRC_DIR)/gateway/utils/gateway_config_helper.cpp \
                    $(SRC_DIR)/gateway/utils/gateway_factory.cpp \
                    $(SRC_DIR)/gateway/utils/stream_start_validator.cpp \
                    $(SRC_DIR)/gateway/utils/source_descriptor.cpp \
                    $(SRC_DIR)/gateway/utils/stream_push_template.cpp \
                    $(SRC_DIR)/gateway/utils/stream_info_detector.cpp \
                    $(SRC_DIR)/gateway/utils/smart_stream_processor.cpp \
                    $(SRC_DIR)/gateway/utils/ffmpeg_process_helper.cpp \
                    $(SRC_DIR)/gateway/utils/bitrate_allocation_helper.cpp

# 所有源文件（基础架构 + Gateway）
ALL_SRC = $(CONFIG_SRC) \
          $(UTILS_SRC) \
          $(STREAMING_SRC) \
          $(STREAMING_MANAGER_SRC) \
          $(PROCESS_SRC) \
          $(MONITORING_SRC) \
          $(API_SRC) \
          $(GATEWAY_UTILS_SRC) \
          $(GATEWAY_RTSP_SRC) \
          $(GATEWAY_RTMP_SRC) \
          $(GATEWAY_HTTPFLV_SRC) \
          $(GATEWAY_DASH_SRC) \
          $(GATEWAY_HLS_SRC) \
          $(GATEWAY_QUIC_SRC) \
          $(GATEWAY_ONVIF_SRC) \
          $(GATEWAY_ISAPI_SRC) \
          $(GATEWAY_DAHUA_SRC) \
          $(GATEWAY_PSIA_SRC) \
          $(GATEWAY_LOCAL_CAMERA_SRC)

# 目标文件
CONFIG_OBJ = $(OBJ_DIR)/config/config_loader.o
UTILS_OBJ = $(OBJ_DIR)/utils/logger.o \
	$(OBJ_DIR)/utils/zlm_stream_checker.o \
	$(OBJ_DIR)/utils/ffmpeg_log_analyzer.o \
            $(OBJ_DIR)/utils/bitrate_allocator.o \
            $(OBJ_DIR)/utils/camera_detector.o \
            $(OBJ_DIR)/utils/stream_status_checker.o \
            $(OBJ_DIR)/utils/http_callback.o \
            $(OBJ_DIR)/utils/zlm_url_builder.o \
            $(OBJ_DIR)/utils/url_parser.o \
            $(OBJ_DIR)/utils/audio_codec_detector.o \
            $(OBJ_DIR)/utils/ffmpeg_params.o \
            $(OBJ_DIR)/utils/system_utils.o
STREAMING_OBJ = $(OBJ_DIR)/streaming/zlmediakit/zlm_client.o \
		$(OBJ_DIR)/streaming/stream_start_queue.o
STREAMING_MANAGER_OBJ = $(OBJ_DIR)/streaming/stream_manager.o
PROCESS_OBJ = $(OBJ_DIR)/process/process_manager.o \
              $(OBJ_DIR)/process/process_monitor.o \
              $(OBJ_DIR)/process/ffmpeg_executor.o \
              $(OBJ_DIR)/process/ffprobe_detector.o
MONITORING_OBJ = $(OBJ_DIR)/monitoring/statistics_manager.o
API_OBJ = $(OBJ_DIR)/api/http_server.o \
          $(OBJ_DIR)/api/websocket_server.o \
          $(OBJ_DIR)/api/utils/response_helper.o \
          $(OBJ_DIR)/api/utils/route_helper.o \
          $(OBJ_DIR)/api/handlers/stream_handler.o \
          $(OBJ_DIR)/api/handlers/device_handler.o \
          $(OBJ_DIR)/api/handlers/statistics_handler.o \
          $(OBJ_DIR)/api/handlers/hook_handler.o \
          $(OBJ_DIR)/api/handlers/process_handler.o
GATEWAY_RTSP_OBJ = $(OBJ_DIR)/gateway/rtsp/rtsp_gateway.o
GATEWAY_RTMP_OBJ = $(OBJ_DIR)/gateway/rtmp/rtmp_gateway.o
GATEWAY_HTTPFLV_OBJ = $(OBJ_DIR)/gateway/httpflv/httpflv_gateway.o
GATEWAY_DASH_OBJ = $(OBJ_DIR)/gateway/dash/dash_gateway.o
GATEWAY_HLS_OBJ = $(OBJ_DIR)/gateway/hls/hls_gateway.o
GATEWAY_QUIC_OBJ = $(OBJ_DIR)/gateway/quic/quic_gateway.o \
                   $(OBJ_DIR)/gateway/quic/quic_server.o \
                   $(OBJ_DIR)/gateway/quic/fec_processor.o \
                   $(OBJ_DIR)/gateway/quic/ts_parser.o \
                   $(OBJ_DIR)/gateway/quic/ts_to_ffmpeg_bridge.o
GATEWAY_ONVIF_OBJ = $(OBJ_DIR)/gateway/onvif/onvif_gateway.o
GATEWAY_ISAPI_OBJ = $(OBJ_DIR)/gateway/isapi/isapi_gateway.o
GATEWAY_DAHUA_OBJ = $(OBJ_DIR)/gateway/dahua/dahua_gateway.o
GATEWAY_PSIA_OBJ = $(OBJ_DIR)/gateway/psia/psia_gateway.o
GATEWAY_GB28181_OBJ = $(OBJ_DIR)/gateway/gb28181/gb28181_gateway.o \
                      $(OBJ_DIR)/gateway/gb28181/gb28181_stream_matcher.o \
                      $(OBJ_DIR)/gateway/gb28181/sip_server.o \
                      $(OBJ_DIR)/gateway/gb28181/sip_message.o
GATEWAY_LOCAL_CAMERA_OBJ = $(OBJ_DIR)/gateway/local_camera/local_camera_gateway.o \
                           $(OBJ_DIR)/gateway/local_camera/device_manager.o \
                           $(OBJ_DIR)/gateway/local_camera/device_resolver.o \
                           $(OBJ_DIR)/gateway/local_camera/stream_validator.o \
                           $(OBJ_DIR)/gateway/local_camera/health_monitor.o
GATEWAY_UTILS_OBJ = \
	$(OBJ_DIR)/gateway/utils/gateway_config_helper.o \
	$(OBJ_DIR)/gateway/utils/gateway_factory.o \
	$(OBJ_DIR)/gateway/utils/stream_start_validator.o \
	$(OBJ_DIR)/gateway/utils/source_descriptor.o \
	$(OBJ_DIR)/gateway/utils/stream_push_template.o \
	$(OBJ_DIR)/gateway/utils/stream_info_detector.o \
	$(OBJ_DIR)/gateway/utils/smart_stream_processor.o \
	$(OBJ_DIR)/gateway/utils/ffmpeg_process_helper.o \
	$(OBJ_DIR)/gateway/utils/bitrate_allocation_helper.o \
    $(OBJ_DIR)/gateway/utils/ffmpeg_command_builder.o

# 所有目标文件
ALL_OBJ = $(CONFIG_OBJ) \
          $(UTILS_OBJ) \
          $(STREAMING_OBJ) \
          $(STREAMING_MANAGER_OBJ) \
          $(PROCESS_OBJ) \
          $(MONITORING_OBJ) \
          $(API_OBJ) \
          $(GATEWAY_UTILS_OBJ) \
          $(GATEWAY_RTSP_OBJ) \
          $(GATEWAY_RTMP_OBJ) \
          $(GATEWAY_HTTPFLV_OBJ) \
          $(GATEWAY_DASH_OBJ) \
          $(GATEWAY_HLS_OBJ) \
          $(GATEWAY_QUIC_OBJ) \
          $(GATEWAY_ONVIF_OBJ) \
          $(GATEWAY_ISAPI_OBJ) \
          $(GATEWAY_DAHUA_OBJ) \
          $(GATEWAY_PSIA_OBJ) \
          $(GATEWAY_GB28181_OBJ) \
          $(GATEWAY_LOCAL_CAMERA_OBJ)

# 可执行文件
GATEWAY_MANAGER = $(BIN_DIR)/gateway_manager

# 默认目标
.PHONY: all clean debug release install deps help sync-zlm-config

all: release

# Debug构建
debug: CXXFLAGS = $(CXXFLAGS_DEBUG) $(INCLUDES) $(FFMPEG_CFLAGS) $(CURL_CFLAGS)
debug: $(GATEWAY_MANAGER) sync-zlm-config

# Release构建
release: CXXFLAGS = $(CXXFLAGS_RELEASE) $(INCLUDES) $(FFMPEG_CFLAGS) $(CURL_CFLAGS)
release: $(GATEWAY_MANAGER) sync-zlm-config

# 创建目录
$(OBJ_DIR):
	@mkdir -p $(OBJ_DIR)/config
	@mkdir -p $(OBJ_DIR)/utils
	@mkdir -p $(OBJ_DIR)/streaming/zlmediakit
	@mkdir -p $(OBJ_DIR)/api/handlers
	@mkdir -p $(OBJ_DIR)/api/handlers/adapters
	@mkdir -p $(OBJ_DIR)/api/utils
	@mkdir -p $(OBJ_DIR)/process
	@mkdir -p $(OBJ_DIR)/monitoring
	@mkdir -p $(OBJ_DIR)/gateway/utils
	@mkdir -p $(OBJ_DIR)/gateway/rtsp
	@mkdir -p $(OBJ_DIR)/gateway/httpflv
	@mkdir -p $(OBJ_DIR)/gateway/dash
	@mkdir -p $(OBJ_DIR)/gateway/hls
	@mkdir -p $(OBJ_DIR)/gateway/onvif
	@mkdir -p $(OBJ_DIR)/gateway/isapi
	@mkdir -p $(OBJ_DIR)/gateway/dahua
	@mkdir -p $(OBJ_DIR)/gateway/psia
	@mkdir -p $(OBJ_DIR)/gateway/gb28181
	@mkdir -p $(OBJ_DIR)/gateway/quic

$(BIN_DIR):
	@mkdir -p $(BIN_DIR)

# 编译规则
$(OBJ_DIR)/%.o: $(SRC_DIR)/%.cpp | $(OBJ_DIR)
	@echo "Compiling $<..."
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -c $< -o $@

# Gateway Manager
$(GATEWAY_MANAGER): $(SRC_DIR)/main.cpp $(ALL_OBJ) | $(BIN_DIR)
	@echo "Linking $@..."
	$(CXX) $(CXXFLAGS) $< $(ALL_OBJ) -o $@ $(LIBS)

# 同步 ZLM 配置文件
sync-zlm-config:
	@echo "同步 ZLM 配置文件..."
	@if [ -f "configs/zlm_config.ini" ]; then \
		echo "  从 configs/zlm_config.ini 同步到所有 ZLM 配置位置..."; \
		cp configs/zlm_config.ini configs/config.ini 2>/dev/null || true; \
		if [ -d "third_party/zlmediakit/release/darwin/Release" ]; then \
			cp configs/zlm_config.ini third_party/zlmediakit/release/darwin/Release/config.ini 2>/dev/null || true; \
		fi; \
		if [ -d "third_party/zlmediakit/release/mac/Release" ]; then \
			cp configs/zlm_config.ini third_party/zlmediakit/release/mac/Release/config.ini 2>/dev/null || true; \
		fi; \
		if [ -f "third_party/zlmediakit/config.ini" ]; then \
			cp configs/zlm_config.ini third_party/zlmediakit/config.ini 2>/dev/null || true; \
		fi; \
		echo "  ✓ 配置文件已同步"; \
	else \
		echo "  ⚠️  警告: configs/zlm_config.ini 不存在，跳过同步"; \
	fi

# 清理
clean:
	@echo "Cleaning..."
	rm -rf $(BUILD_DIR) $(BIN_DIR)

# 安装依赖（Ubuntu/Debian）
deps-ubuntu:
	@echo "Installing dependencies for Ubuntu/Debian..."
	sudo apt-get update
	sudo apt-get install -y \
		build-essential \
		g++ \
		pkg-config \
		libavformat-dev \
		libavcodec-dev \
		libavutil-dev \
		libavfilter-dev \
		libswscale-dev \
		libcurl4-openssl-dev \


# 安装依赖（macOS）
deps-macos:
	@echo "Installing dependencies for macOS..."
	brew install cmake pkg-config ffmpeg curl

# 检查依赖
check-deps:
	@echo "Checking dependencies..."
	@echo -n "FFmpeg开发库: "
	@$(PKG_CONFIG) --exists libavformat && echo "OK" || echo "NOT FOUND"
	@echo -n "FFmpeg二进制: "
	@if [ -f "$(FFMPEG_BINARY)" ]; then \
		echo "OK ($(FFMPEG_BINARY))"; \
	elif command -v ffmpeg >/dev/null 2>&1; then \
		echo "OK (system: $$(command -v ffmpeg))"; \
	else \
		echo "NOT FOUND ($(FFMPEG_BINARY) or system)"; \
	fi
	@echo -n "FFprobe二进制: "
	@if [ -f "$(FFPROBE_BINARY)" ]; then \
		echo "OK ($(FFPROBE_BINARY))"; \
	elif command -v ffprobe >/dev/null 2>&1; then \
		echo "OK (system: $$(command -v ffprobe))"; \
	else \
		echo "NOT FOUND ($(FFPROBE_BINARY) or system)"; \
	fi
	@echo -n "libcurl: "
	@$(PKG_CONFIG) --exists libcurl && echo "OK" || echo "NOT FOUND"


# 安装
install: release
	@echo "Installing..."
	@mkdir -p /usr/local/bin
	@mkdir -p /etc/gateway
	@mkdir -p /var/log/zlm-gateway
	cp $(BIN_DIR)/gateway_manager /usr/local/bin/
	cp configs/*.json /etc/gateway/ 2>/dev/null || true
	cp configs/*.conf /etc/gateway/ 2>/dev/null || true
	# 安装 systemd 服务
	@if [ -f scripts/service/zlm-gateway.service ]; then \
		echo "Installing systemd service..."; \
		cp scripts/service/zlm-gateway.service /etc/systemd/system/; \
		systemctl daemon-reload || true; \
		echo "Service installed. Enable with: systemctl enable zlm-gateway"; \
	fi

# 帮助信息
help:
	@echo "ZLMediaKit Gateway - Makefile"
	@echo ""
	@echo "Targets:"
	@echo "  all              - Build release version (default)"
	@echo "  release          - Build release version"
	@echo "  debug            - Build debug version"
	@echo "  clean            - Clean build files"
	@echo "  sync-zlm-config  - Sync ZLM config file to all locations"
	@echo "  deps-ubuntu      - Install dependencies (Ubuntu/Debian)"
	@echo "  deps-macos       - Install dependencies (macOS)"
	@echo "  check-deps       - Check if dependencies are installed"
	@echo "  install          - Install binaries and configs"
	@echo "  help             - Show this help message"
	@echo ""
	@echo "Examples:"
	@echo "  make              # Build release version"
	@echo "  make debug        # Build debug version"
	@echo "  make clean        # Clean build files"
	@echo "  make deps-ubuntu  # Install dependencies"
