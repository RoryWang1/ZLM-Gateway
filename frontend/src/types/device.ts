// 设备相关类型定义

export type DeviceProtocol = 'onvif' | 'isapi' | 'dahua' | 'psia' | 'local-camera' | 'gb28181';

export interface Device {
  device_id: string;
  name: string;
  ip?: string;  // 本地摄像头没有IP
  port?: number;  // 本地摄像头没有端口
  manufacturer?: string;
  model?: string;
  protocol: DeviceProtocol;
  serial_number?: string;
  platform?: string;  // 本地摄像头平台信息
  device_path?: string;  // 本地摄像头设备路径
  index?: number;  // 本地摄像头索引
  online?: boolean;  // 设备在线状态（GB28181等协议使用）
  channels?: string[];  // 通道列表（GB28181等协议使用）
  username?: string;  // 认证用户名
  password?: string;  // 认证密码
  [key: string]: any;  // 允许协议特定的扩展字段
}

export interface DeviceStream {
  stream_url: string;
  profile?: string;
  resolution?: string;
}

