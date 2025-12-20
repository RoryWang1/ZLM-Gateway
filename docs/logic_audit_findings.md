# Logic Audit Findings & Stability Analysis

**Date**: 2025-12-20
**Status**: Root Cause Identified for All Issues

## 1. Overview
This document consolidates findings from the comprehensive codebase logic audit, triggered by the "WebRTC Green Screen" issue. The audit revealed deeper instability patterns in process management and configuration logic.

---

## 2. WebRTC Green Screen (Logic Flaw)

### Problem
Network streams (RTSP/RTMP) appeared green when viewed via WebRTC, while local camera streams worked correctly.

### Root Cause
**Blind Assumption in FFmpeg Command**:
The `FFmpegCommandBuilder` assumed that **any** H.264 input could be directly copied (`-c:v copy`) to WebRTC output.
*   **WebRTC Requirement**: Strict H.264 Baseline Profile + `yuv420p` pixel format.
*   **Reality**: Many network streams use High Profile or `yuvj420p`.
*   **Failure**: Incompatible streams were copied directly, causing browser decoders to fail (green screen).

### Fix Strategy
*   Enforce strict compatibility checks (`IsVideoWebRTCCompatible`) validation Profile and Pixel Format.
*   Force transcoding for any H.264 stream that isn't Baseline + `yuv420p`.

---

## 3. Resolution Config Ignored (Logic Flaw)

### Problem
User-configured resolution scaling (e.g., downsizing 4K to 1080p) is silently ignored for H.264 input streams.

### Root Cause
**Priority Inversion**:
In `FFmpegCommandBuilder`, the check for "Video Compatibility" (can we copy?) happens **before** checking if resolution scaling is needed.
*   If input is H.264, code sets `-c:v copy`.
*   `-c:v copy` inherently bypasses all video filters, including scaling (`-vf scale`).

### Fix Strategy
*   Update copy condition: `if (video_compatible && resolution_matches)`.
*   Only allow copy if the target resolution is not set or matches the input resolution.

---

## 4. Stability Critical: Restart Failure (Address/Protocol Error)

### Problem
1.  **Zombie Processes**: Streams failed to restart with "Address already in use" because `ffmpeg` continued running after the Gateway stopped.
2.  **Protocol Mismatch (400 Error)**: Local camera streams failed to restart with "Unsupported protocol" log error.

### Root Cause
1.  **Zombie**: `ProcessManager` used `sh -c "ffmpeg ..."` which only killed the shell, leaving `ffmpeg` orphaned.
2.  **Protocol**: Frontend requested `local-camera` (hyphen), but Gateway registered `local_camera` (underscore).

### Fix Implementation
1.  **Process Manager**: Prepend `exec` to commands: `sh -c "exec ffmpeg ..."`. This replaces the shell process ensuring signals reach FFmpeg.
2.  **Alias Fix**: Added `StreamHandler::SelectGateway` alias to map `local-camera` -> `local_camera`.

---

## 5. Stability Critical: Auto-Stop Cycle (60s/Timeout)

### Problem
Streams automatically stopped after 60 seconds (ZLM default) even with the `HookHandler` fix applied.

### Root Cause
1.  **ZLM Timers**: ZLMediaKit's internal `streamNoneReaderDelayMS` timer (60s) triggers shutdown aggressively, sometimes bypassing or conflicting with hook logic.
2.  **Zombie ZLM Process**: A "Zombie" `MediaServer` process was running in the background, preventing `zlm_config.ini` updates from taking effect during service restarts.

### Fix Implementation
1.  **Hook Logic**: Explicitly return `{"close": false}` in `on_stream_none_reader`.
2.  **Configuration Override (Definitive)**: Set `streamNoneReaderDelayMS = 31536000000` (1 year) in `zlm_config.ini` to effectively disable the internal idle check.
3.  **Zombie Cleanup**: Identified and killed the zombie `MediaServer` process to ensuring the new configuration (1-year timeout) was successfully loaded.

---
