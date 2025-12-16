// PSIA 协议适配器

import { apiClient } from '@/api/client';
import type { IProtocolAdapter, DiscoveryOptions, ProtocolConfig } from '../types';
import type { Device, DeviceStream, DeviceProtocol } from '@/types/device';
import type { Protocol } from '@/types/stream';

export class PSIAAdapter implements IProtocolAdapter {
  readonly protocol: DeviceProtocol = 'psia';

  readonly config: ProtocolConfig = {
    label: 'PSIA',
    value: 'psia',
    description: 'PSIA标准协议',
    requiresAuth: true,
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
      {
        name: 'username',
        label: '用户名',
        type: 'text',
        required: true,
        placeholder: '例如: admin',
        rules: [{ required: true, message: '请输入用户名' }],
      },
      {
        name: 'password',
        label: '密码',
        type: 'password',
        required: true,
        placeholder: '请输入密码',
        rules: [{ required: true, message: '请输入密码' }],
      },
    ],
  };

  async discover(options: DiscoveryOptions): Promise<Device[]> {
    if (!options.ipRange || !options.username || !options.password) {
      throw new Error('IP range, username and password are required for PSIA discovery');
    }
    const response = await apiClient.post<{ devices: Device[]; count: number }>('/psia/devices/discover', {
      ip_range: options.ipRange,
      port: options.port || 80,
      username: options.username,
      password: options.password,
    });
    return response.devices || [];
  }

  async getDevices(): Promise<Device[]> {
    const response = await apiClient.get<{ devices: Device[]; count: number }>('/psia/devices');
    return response.devices || [];
  }

  async getDevice(deviceId: string): Promise<Device> {
    return apiClient.get<Device>(`/psia/devices/${deviceId}`);
  }

  async addDevice(device: Partial<Device>): Promise<Device> {
    throw new Error('PSIA device manual add not implemented');
  }

  async deleteDevice(deviceId: string): Promise<void> {
    return apiClient.delete<void>(`/psia/devices/${deviceId}`);
  }

  async getDeviceStreams(deviceId: string): Promise<DeviceStream[]> {
    return apiClient.get<DeviceStream[]>(`/psia/devices/${deviceId}/rtsp-urls`);
  }

  async startDeviceStream(
    deviceId: string,
    app: string,
    stream: string,
    options?: { channelId?: string; outputProtocol?: Protocol }
  ): Promise<void> {
    return apiClient.post<void>(`/psia/devices/${deviceId}/streams/start`, {
      app,
      stream,
    });
  }
}

