// 常量定义

export const API_BASE_URL = import.meta.env.VITE_API_BASE_URL || '/api/v1';
export const ZLM_BASE_URL = import.meta.env.VITE_ZLM_BASE_URL || 'http://localhost:8081';
export const ZLM_SECRET = import.meta.env.VITE_ZLM_SECRET || '';


// 协议类型
export const NATIVE_PROTOCOLS = ['rtsp', 'rtmp'] as const;
export const NON_NATIVE_PROTOCOLS = ['http-flv', 'dash', 'hls', 'quic'] as const;
export const DEVICE_PROTOCOLS = ['onvif', 'isapi', 'dahua', 'psia', 'local-camera', 'gb28181'] as const;

// 协议标签颜色
export const PROTOCOL_COLORS = {
  native: 'green',
  'non-native': 'blue',
  device: 'orange',
} as const;

// 状态颜色
export const STATUS_COLORS = {
  running: 'success',
  stopped: 'default',
  starting: 'processing',
  stopping: 'warning',
  error: 'error',
} as const;

