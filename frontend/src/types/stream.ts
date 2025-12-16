// 流相关类型定义

export type Protocol = 'rtsp' | 'rtmp' | 'http-flv' | 'dash' | 'hls' | 'quic' | 'onvif' | 'isapi' | 'dahua' | 'psia' | 'webrtc' | 'local-camera' | 'gb28181';

export type StreamStatus = 'stopped' | 'starting' | 'running' | 'stopping' | 'error';

export type GatewayType = 'native' | 'ffmpeg' | 'device' | 'httpflv_gateway' | 'hls_gateway' | 'rtsp_gateway' | 'dash_gateway' | 'quic_gateway' | 'onvif_gateway' | 'isapi_gateway' | 'dahua_gateway' | 'psia_gateway' | 'local_camera_gateway' | 'gb28181_gateway';

export interface Stream {
  app: string;
  stream: string;
  protocol: Protocol;
  output_protocol?: Protocol;
  gateway_type?: GatewayType;
  processing_type?: string; // "0"(Direct), "1"(Copy), "2"(Transcode)
  /**
   * Numeric status code from backend (e.g., 2 for running)
   */
  status: number | StreamStatus;
  /**
   * Human‑readable status text from backend (e.g., "running")
   */
  status_text?: string;
  source_url: string;
  device_id?: string;  // 设备ID，用于设备相关的流（如本地摄像头）

  pid?: number;
  cpu_usage?: number;
  memory_usage?: number;
  reader_count?: number;
  bytes_speed?: number;
  total_bytes?: number;
  uptime_seconds?: number;
  zlm_alive?: boolean;
  error_code?: string;
  error_message?: string;
}

export interface StartStreamRequest {
  protocol: Protocol;
  output_protocol: Protocol;
  source_url: string;
  app: string;
  stream: string;
}

export interface StopStreamRequest {
  protocol: Protocol;
  app: string;
  stream: string;
}

