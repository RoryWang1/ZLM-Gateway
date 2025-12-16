// ONVIF 协议适配器

import { apiClient } from '@/api/client';
import type { IProtocolAdapter, DiscoveryOptions, ProtocolConfig } from '../types';
import type { Device, DeviceStream, DeviceProtocol } from '@/types/device';
import type { Protocol } from '@/types/stream';

export class ONVIFAdapter implements IProtocolAdapter {
  readonly protocol: DeviceProtocol = 'onvif';

  readonly config: ProtocolConfig = {
    label: 'ONVIF',
    value: 'onvif',
    description: 'ONVIF标准协议，支持自动发现',
    requiresAuth: false,
    requiresIPRange: false,
    formFields: [
      {
        name: 'timeoutSeconds',
        label: '超时时间（秒）',
        type: 'number',
        required: true,
        defaultValue: 5,
        rules: [
          { required: true, message: '请输入超时时间' },
          { type: 'number', min: 1, max: 30, message: '超时时间范围：1-30秒' },
        ],
      },
    ],
  };

  async discover(options: DiscoveryOptions): Promise<Device[]> {
    const response = await apiClient.post<{ devices: Device[]; count: number }>('/devices/discover', {
      timeout_seconds: options.timeoutSeconds || 5,
    });
    return response.devices || [];
  }

  async getDevices(): Promise<Device[]> {
    const response = await apiClient.get<{ devices: Device[]; count: number }>('/devices');
    return response.devices || [];
  }

  async getDevice(deviceId: string): Promise<Device> {
    return apiClient.get<Device>(`/devices/${deviceId}`);
  }

  async addDevice(device: Partial<Device>): Promise<Device> {
    const response = await apiClient.post<{ device_id: string; xaddr: string }>('/devices/add', device);
    return this.getDevice(response.device_id);
  }

  async deleteDevice(deviceId: string): Promise<void> {
    return apiClient.delete<void>(`/devices/${deviceId}`);
  }

  async getDeviceStreams(deviceId: string): Promise<DeviceStream[]> {
    return apiClient.get<DeviceStream[]>(`/devices/${deviceId}/rtsp-urls`);
  }

  async startDeviceStream(
    deviceId: string,
    app: string,
    stream: string,
    options?: { channelId?: string; outputProtocol?: Protocol }
  ): Promise<void> {
    return apiClient.post<void>(`/devices/${deviceId}/streams/start`, {
      app,
      stream,
    });
  }
}

