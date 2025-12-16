// 协议注册表 - 管理所有协议适配器

import type { IProtocolAdapter } from './types';
import type { DeviceProtocol } from '@/types/device';

class ProtocolRegistry {
  private adapters: Map<DeviceProtocol, IProtocolAdapter> = new Map();

  /**
   * 注册协议适配器
   */
  register(adapter: IProtocolAdapter): void {
    this.adapters.set(adapter.protocol, adapter);
  }

  /**
   * 获取协议适配器
   */
  get(protocol: DeviceProtocol): IProtocolAdapter | undefined {
    return this.adapters.get(protocol);
  }

  /**
   * 获取所有已注册的协议
   */
  getAll(): IProtocolAdapter[] {
    return Array.from(this.adapters.values());
  }

  /**
   * 获取所有协议类型
   */
  getProtocols(): DeviceProtocol[] {
    return Array.from(this.adapters.keys());
  }

  /**
   * 检查协议是否已注册
   */
  has(protocol: DeviceProtocol): boolean {
    return this.adapters.has(protocol);
  }
}

// 全局协议注册表实例
export const protocolRegistry = new ProtocolRegistry();

