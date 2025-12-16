// Local Camera 协议适配器

import { apiClient } from '@/api/client';
import type { IProtocolAdapter, DiscoveryOptions, ProtocolConfig } from '../types';
import type { Device, DeviceStream, DeviceProtocol } from '@/types/device';
import type { Protocol } from '@/types/stream';

export class LocalCameraAdapter implements IProtocolAdapter {
  readonly protocol: DeviceProtocol = 'local-camera';

  readonly config: ProtocolConfig = {
    label: '本地摄像头',
    value: 'local-camera',
    description: '系统本地USB摄像头设备',
    requiresAuth: false,
    requiresIPRange: false,
    formFields: [],
  };

  async discover(options: DiscoveryOptions): Promise<Device[]> {
    const response = await apiClient.post<{ devices: Device[]; count: number }>('/local-camera/devices/discover', {});
    return response.devices || [];
  }

  async getDevices(): Promise<Device[]> {
    const response = await apiClient.get<{ devices: Device[]; count: number }>('/local-camera/devices');
    return response.devices || [];
  }

  async getDevice(deviceId: string): Promise<Device> {
    return apiClient.get<Device>(`/local-camera/devices/${deviceId}`);
  }

  async addDevice(device: Partial<Device>): Promise<Device> {
    throw new Error('Local camera devices are auto-discovered, cannot be manually added');
  }

  async deleteDevice(deviceId: string): Promise<void> {
    throw new Error('Local camera devices cannot be deleted');
  }

  async refreshDevices(): Promise<Device[]> {
    const response = await apiClient.post<{ devices: Device[]; count: number }>('/local-camera/devices/refresh', {});
    return response.devices || [];
  }

  async startDeviceStream(
    deviceId: string,
    app: string,
    stream: string,
    options?: { channelId?: string; outputProtocol?: Protocol }
  ): Promise<void> {
    return apiClient.post<void>('/local-camera/streams/start', {
      device_id: deviceId,
      app,
      stream,
      output_protocol: options?.outputProtocol || 'webrtc',
    });
  }

  async stopDeviceStream(app: string, stream: string): Promise<void> {
    return apiClient.post<void>('/local-camera/streams/stop', {
      app,
      stream,
    });
  }
}

