// 协议类型定义

import type { Device, DeviceStream, DeviceProtocol } from '@/types/device';
import type { Protocol } from '@/types/stream';

/**
 * 协议发现选项
 */
export interface DiscoveryOptions {
  timeoutSeconds?: number;
  ipRange?: string;
  port?: number;
  username?: string;
  password?: string;
  [key: string]: any; // 允许协议特定的选项
}

/**
 * 协议配置选项（用于发现表单）
 */
export interface ProtocolConfig {
  label: string;
  value: DeviceProtocol;
  icon?: string;
  description?: string;
  requiresAuth?: boolean;
  requiresIPRange?: boolean;
  defaultPort?: number;
  defaultIPRange?: string;
  formFields?: ProtocolFormField[];
}

/**
 * 协议表单字段定义
 */
export interface ProtocolFormField {
  name: string;
  label: string;
  type: 'text' | 'number' | 'password' | 'select';
  required?: boolean;
  defaultValue?: any;
  placeholder?: string;
  options?: { label: string; value: any }[];
  rules?: any[];
  helpText?: string;
}

/**
 * 协议适配器接口 - 所有协议必须实现此接口
 */
export interface IProtocolAdapter {
  /**
   * 协议类型
   */
  readonly protocol: DeviceProtocol;

  /**
   * 协议配置信息
   */
  readonly config: ProtocolConfig;

  /**
   * 发现设备
   */
  discover(options: DiscoveryOptions): Promise<Device[]>;

  /**
   * 获取设备列表
   */
  getDevices(): Promise<Device[]>;

  /**
   * 获取单个设备信息
   */
  getDevice(deviceId: string): Promise<Device>;

  /**
   * 添加设备
   */
  addDevice(device: Partial<Device>): Promise<Device>;

  /**
   * 删除设备
   */
  deleteDevice(deviceId: string): Promise<void>;

  /**
   * 获取设备流地址列表
   */
  getDeviceStreams?(deviceId: string): Promise<DeviceStream[]>;

  /**
   * 启动设备流
   */
  startDeviceStream(
    deviceId: string,
    app: string,
    stream: string,
    options?: {
      channelId?: string;
      outputProtocol?: Protocol;
      [key: string]: any;
    }
  ): Promise<void>;

  /**
   * 停止设备流
   */
  stopDeviceStream?(app: string, stream: string): Promise<void>;

  /**
   * 刷新设备列表（如果支持）
   */
  refreshDevices?(): Promise<Device[]>;
}

