// ISAPI 协议适配器

import { apiClient } from '@/api/client';
import type { IProtocolAdapter, DiscoveryOptions, ProtocolConfig } from '../types';
import type { Device, DeviceStream, DeviceProtocol } from '@/types/device';
import type { Protocol } from '@/types/stream';

export class ISAPIAdapter implements IProtocolAdapter {
  readonly protocol: DeviceProtocol = 'isapi';

  readonly config: ProtocolConfig = {
    label: 'ISAPI',
    value: 'isapi',
    description: '海康威视ISAPI协议',
    requiresAuth: false,
    requiresIPRange: true,
    defaultPort: 80,
    defaultIPRange: '192.168.1.0/24',
    formFields: [
      {
        name: 'ipRange',
        label: 'IP范围',
        type: 'text',
        required: true,
        defaultValue: '192.168.1.0/24',
        placeholder: '例如: 192.168.1.0/24',
        rules: [{ required: true, message: '请输入IP范围' }],
      },
      {
        name: 'port',
        label: '端口',
        type: 'number',
        required: true,
        defaultValue: 80,
        rules: [
          { required: true, message: '请输入端口' },
          { type: 'number', min: 1, max: 65535, message: '端口范围：1-65535' },
        ],
      },
    ],
  };

  async discover(options: DiscoveryOptions): Promise<Device[]> {
    if (!options.ipRange) {
      throw new Error('IP range is required for ISAPI discovery');
    }
    const response = await apiClient.post<{ devices: Device[]; count: number }>('/isapi/devices/discover', {
      ip_range: options.ipRange,
      port: options.port || 80,
    });
    return response.devices || [];
  }

  async getDevices(): Promise<Device[]> {
    const response = await apiClient.get<{ devices: Device[]; count: number }>('/isapi/devices');
    return response.devices || [];
  }

  async getDevice(deviceId: string): Promise<Device> {
    return apiClient.get<Device>(`/isapi/devices/${deviceId}`);
  }

  async addDevice(device: Partial<Device>): Promise<Device> {
    // ISAPI设备通常通过发现添加，手动添加功能待实现
    throw new Error('ISAPI device manual add not implemented');
  }

  async deleteDevice(deviceId: string): Promise<void> {
    return apiClient.delete<void>(`/isapi/devices/${deviceId}`);
  }

  async getDeviceStreams(deviceId: string): Promise<DeviceStream[]> {
    return apiClient.get<DeviceStream[]>(`/isapi/devices/${deviceId}/rtsp-urls`);
  }

  async startDeviceStream(
    deviceId: string,
    app: string,
    stream: string,
    options?: { channelId?: string; outputProtocol?: Protocol }
  ): Promise<void> {
    return apiClient.post<void>(`/isapi/devices/${deviceId}/streams/start`, {
      app,
      stream,
    });
  }
}

