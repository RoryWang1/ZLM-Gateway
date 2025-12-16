# QUIC Gateway Implementation Plan

## Overview

Implement a QUIC Gateway to receive QUIC-encapsulated FEC-encoded MPEG-TS streams and integrate them into the ZLM Gateway ecosystem.

## Technical Architecture

```
┌─────────────┐     QUIC+FEC+TS      ┌──────────────────┐
│ QUIC Source │ ──────────────────▶  │  QUIC Gateway    │
│ (Encoder)   │                      │                  │
└─────────────┘                      │ 1. QUIC Server   │
                                     │ 2. FEC Decoder   │
                                     │ 3. TS Demux      │
                                     │ 4. Transcoder    │
                                     └────────┬─────────┘
                                              │ RTMP Push
                                              ▼
                                     ┌─────────────────┐
                                     │   ZLMediaKit    │
                                     │  RTSP Server    │
                                     └─────────────────┘
```

## Implementation Phases

### Phase 1: QUIC Server Foundation (Week 1)

#### 1.1 Library Selection & Integration
- **Primary Option**: `quiche` (Cloudflare, Rust-based with C API)
  - Pros: Production-ready, well-maintained, C API available
  - Cons: Requires Rust toolchain for building
- **Alternative**: `msquic` (Microsoft, C)
  - Pros: Native C, Windows/Linux support
  - Cons: More complex API

#### 1.2 Dependencies
```bash
# quiche dependencies
- Rust toolchain (cargo, rustc)
- BoringSSL or OpenSSL
- CMake for integration

# msquic dependencies  
- OpenSSL 1.1+
- CMake
```

#### 1.3 Core Components
```cpp
// src/gateway/quic/quic_server.hpp
class QuicServer {
public:
    QuicServer(const std::string& bind_addr, uint16_t port);
    
    bool Start();
    void Stop();
    
    // Callback for new QUIC connections
    void SetConnectionCallback(
        std::function<void(QuicConnection*)> callback);
    
private:
    quiche_config* config_;
    int socket_fd_;
    std::thread accept_thread_;
};

// src/gateway/quic/quic_connection.hpp
class QuicConnection {
public:
    // Receive data from QUIC stream
    size_t Receive(uint8_t* buffer, size_t max_len);
    
    // Get stream ID
    uint64_t GetStreamId() const;
    
private:
    quiche_conn* conn_;
    uint64_t stream_id_;
};
```

#### 1.4 Build System Updates
```makefile
# Makefile additions
QUICHE_LIB = third_party/quiche/lib/libquiche.a
QUICHE_INCLUDE = third_party/quiche/include

CXXFLAGS += -I$(QUICHE_INCLUDE)
LDFLAGS += $(QUICHE_LIB) -lpthread -ldl -lm
```

### Phase 2: FEC Decoder Integration (Week 2)

#### 2.1 FEC Algorithm Determination
**Need to identify which FEC algorithm is used:**
- **SMPTE 2022-1** (Pro-MPEG Code of Practice #3)
- **RaptorQ** (RFC 6330)
- **Reed-Solomon**
- Custom implementation

#### 2.2 FEC Decoder Options

**Option A: SMPTE 2022-1** (Most common for broadcast)
```cpp
// Use existing library: libfec or implement per SMPTE spec
class ProMPEGFECDecoder {
public:
    // Configure L x D matrix
    void Configure(int L, int D);
    
    // Input FEC-protected packets
    void InputPacket(const uint8_t* data, size_t len, 
                     uint16_t seq_num, bool is_fec);
    
    // Output recovered TS packets
    bool GetRecoveredPacket(uint8_t* output, size_t* len);
    
private:
    // Column FEC and Row FEC buffers
    std::map<uint16_t, Packet> media_packets_;
    std::map<uint16_t, Packet> fec_packets_;
};
```

**Option B: RaptorQ**
```cpp
// Use libraptorq
#include <RaptorQ/RaptorQ.hpp>

class RaptorQDecoder {
    // High-level API for fountain codes
};
```

#### 2.3 Integration Points
```cpp
// src/gateway/quic/fec_processor.hpp
class FECProcessor {
public:
    FECProcessor(FECAlgorithm algo);
    
    // Process incoming QUIC stream data
    void ProcessQuicData(const uint8_t* data, size_t len);
    
    // Output callback for recovered TS packets
    void SetOutputCallback(
        std::function<void(const uint8_t*, size_t)> callback);
    
private:
    std::unique_ptr<FECDecoder> decoder_;
    CircularBuffer output_buffer_;
};
```

### Phase 3: TS Processing Pipeline (Week 2-3)

#### 3.1 TS Packet Parser
```cpp
// src/gateway/quic/ts_parser.hpp
class TSParser {
public:
    struct Packet {
        uint8_t sync_byte;        // 0x47
        uint16_t pid;
        bool payload_start;
        uint8_t continuity_counter;
        std::vector<uint8_t> payload;
    };
    
    // Parse TS packets from FEC-recovered data
    bool ParsePacket(const uint8_t* data, Packet* packet);
    
    // Extract PES packets and demux A/V
    bool DemuxAV(const Packet& ts_packet);
};
```

#### 3.2 FFmpeg Integration
```cpp
// src/gateway/quic/ts_to_ffmpeg_bridge.hpp
class TSToFFmpegBridge {
public:
    TSToFFmpegBridge(const std::string& output_rtsp_url);
    
    // Create pipe for FFmpeg input
    bool CreatePipe();
    
    // Write TS packets to pipe
    void WritePacket(const uint8_t* data, size_t len);
    
    // Start FFmpeg process
    bool StartFFmpeg(const StreamInfo& info);
    
private:
    int pipe_fd_;
    pid_t ffmpeg_pid_;
    std::string pipe_path_;  // e.g., /tmp/quic_ts_pipe_<stream_id>
};
```

#### 3.3 FFmpeg Command Construction
```cpp
std::string BuildFFmpegCommand() {
    // Read from named pipe, transcode if needed
    std::ostringstream cmd;
    cmd << ffmpeg_path_ << " "
        << "-re "                    // Real-time
        << "-f mpegts "              // Input format
        << "-i " << pipe_path_ << " " // Input from pipe
        << "-c:v libx264 "          // Transcode video
        << "-preset ultrafast "
        << "-tune zerolatency "
        << "-b:v 2M "
        << "-c:a aac "              // Transcode audio
        << "-f rtsp "               // Output to RTSP
        << "-rtsp_transport tcp "
        << "\"" << rtsp_url_ << "\"";
    
    return cmd.str();
}
```

### Phase 4: Gateway Integration (Week 3)

#### 4.1 QUICGateway Class
```cpp
// src/gateway/quic/quic_gateway.cpp
class QUICGateway : public IGateway {
public:
    QUICGateway(
        std::shared_ptr<config::Config> config,
        std::shared_ptr<streaming::ZLMClient> zlm_client,
        std::shared_ptr<process::ProcessManager> process_manager);
    
    bool Start(const StreamInfo& info) override;
    bool Stop(const std::string& app, const std::string& stream) override;
    
private:
    // QUIC server instance (one per Gateway, multi-connection)
    std::unique_ptr<QuicServer> server_;
    
    // Map of active streams
    std::map<std::string, StreamContext> active_streams_;
    
    struct StreamContext {
        std::unique_ptr<QuicConnection> connection;
        std::unique_ptr<FECProcessor> fec_processor;
        std::unique_ptr<TSParser> ts_parser;
        std::unique_ptr<TSToFFmpegBridge> ffmpeg_bridge;
        pid_t ffmpeg_pid;
    };
};
```

#### 4.2 Stream Lifecycle
```cpp
bool QUICGateway::Start(const StreamInfo& info) {
    // 1. Parse source_url to get QUIC connection info
    //    Format: quic://<host>:<port>/<stream_id>
    
    // 2. Create QUIC connection (or reuse existing server)
    auto connection = server_->AcceptConnection();
    
    // 3. Setup FEC processor
    auto fec_proc = std::make_unique<FECProcessor>(FECAlgorithm::SMPTE_2022_1);
    
    // 4. Setup TS parser
    auto ts_parser = std::make_unique<TSParser>();
    
    // 5. Create FFmpeg bridge
    auto ffmpeg_bridge = std::make_unique<TSToFFmpegBridge>(
        BuildRTSPUrl(info));
    
    // 6. Wire up the pipeline
    connection->SetDataCallback([fec_proc](const uint8_t* data, size_t len) {
        fec_proc->ProcessQuicData(data, len);
    });
    
    fec_proc->SetOutputCallback([ts_parser](const uint8_t* data, size_t len) {
        ts_parser->ParseAndDemux(data, len);
    });
    
    ts_parser->SetOutputCallback([ffmpeg_bridge](const uint8_t* data, size_t len) {
        ffmpeg_bridge->WritePacket(data, len);
    });
    
    // 7. Start FFmpeg process
    ffmpeg_bridge->StartFFmpeg(info);
    
    // 8. Register stream
    return RegisterStream(info);
}
```

## Configuration Schema

```json
{
  "quic": {
    "enabled": true,
    "bind_address": "0.0.0.0",
    "port": 4433,
    "max_connections": 100,
    "fec": {
      "algorithm": "smpte_2022_1",
      "params": {
        "L": 10,
        "D": 10
      }
    },
    "tls": {
      "cert_path": "certs/server.crt",
      "key_path": "certs/server.key"
    }
  }
}
```

## Testing Strategy

### Unit Tests
```cpp
// tests/quic_gateway_test.cpp
TEST(QUICGateway, AcceptConnection) { }
TEST(FECProcessor, RecoverLostPackets) { }
TEST(TSParser, ParseValidPacket) { }
```

### Integration Tests
```bash
# 1. Setup mock QUIC source
./tools/quic_stream_simulator --fec-type smpte2022 --output quic://localhost:4433/test

# 2. Start Gateway
./bin/gateway_manager

# 3. Add stream via API
curl -X POST http://localhost:8088/api/v1/streams/start \
  -d '{"app":"live","stream":"quic_test","source_url":"quic://localhost:4433/test","protocol":"quic"}'

# 4. Verify RTSP stream
ffplay rtsp://localhost:5554/live/quic_test
```

## Potential Issues & Mitigations

### 1. QUIC Connection Management
**Issue**: Multiple streams on same QUIC connection vs separate connections
**Mitigation**: Support both modes, configurable per deployment

### 2. FEC Overhead
**Issue**: High CPU usage for FEC decoding
**Mitigation**: 
- Use optimized SIMD implementations
- Consider hardware offload if available
- Buffer management to prevent memory bloat

### 3. Timing & Synchronization
**Issue**: QUIC reordering + FEC recovery can introduce jitter
**Mitigation**:
- Implement dejitter buffer
- Use PTS/DTS from TS for A/V sync
- Configurable buffer size

### 4. Error Handling
**Issue**: Network interruptions, partial FEC recovery
**Mitigation**:
- Graceful degradation (forward packets even if FEC fails)
- Monitoring & alerting for packet loss
- Auto-restart on connection failure

## Dependencies to Add

```
third_party/
├── quiche/          # QUIC implementation
├── libfec/          # FEC library (if using SMPTE 2022-1)
└── libraptorq/      # Alternative FEC (if using RaptorQ)
```

## Timeline Summary

| Phase | Duration | Key Deliverables |
|-------|----------|-----------------|
| 1. QUIC Server | 1 week | Working QUIC server, connection handling |
| 2. FEC Decoder | 1 week | FEC recovery, TS packet output |
| 3. TS Processing | 1 week | TS parsing, FFmpeg pipeline |
| 4. Integration | 1 week | Full Gateway integration, testing |

**Total: 3-4 weeks**

## Open Questions

1. **FEC Algorithm Specification**
   - Which FEC algorithm is actually being used?
   - What are the FEC parameters (L, D for 2D parity)?
   - Any custom FEC implementation?

2. **QUIC Stream Structure**
   - Single bidirectional stream or unidirectional?
   - Multiple streams per connection?
   - Custom QUIC application protocol?

3. **TS Stream Properties**
   - Bitrate range?
   - Codec types (H.264, H.265, etc.)?
   - Audio codec (AAC, MP3)?

4. **Testing Infrastructure**
   - Access to real QUIC+FEC+TS source?
   - Need to develop simulator?

## Next Steps

1. **Requirements Gathering**
   - Get FEC algorithm specification
   - Obtain sample QUIC+FEC+TS stream or protocol docs
   - Clarify performance requirements (latency, throughput)

2. **Prototype Phase**
   - Build minimal QUIC receiver (Phase 1)
   - Test with real source if available
   - Validate FEC recovery works

3. **Full Implementation**
   - Follow phase plan above
   - Integrate with existing Gateway infrastructure
   - Comprehensive testing

## References

- QUIC Protocol: RFC 9000
- SMPTE 2022-1: Forward Error Correction for Real-Time Video/Audio Transport
- ISO/IEC 13818-1: MPEG-2 Transport Stream
- quiche documentation: https://github.com/cloudflare/quiche
- msquic documentation: https://github.com/microsoft/msquic
