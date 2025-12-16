// GB28181 协议适配器

import { apiClient } from '@/api/client';
import type { IProtocolAdapter, DiscoveryOptions, ProtocolConfig } from '../types';
import type { Device, DeviceStream, DeviceProtocol } from '@/types/device';
import type { Protocol } from '@/types/stream';

export class GB28181Adapter implements IProtocolAdapter {
  readonly protocol: DeviceProtocol = 'gb28181';

  readonly config: ProtocolConfig = {
    label: 'GB28181',
    value: 'gb28181',
    description: 'GB28181国标协议，支持SIP注册',
    requiresAuth: false,
    requiresIPRange: false,
    formFields: [
      {
        name: 'id',
        label: '设备ID',
        type: 'text',
        required: true,
        placeholder: '20位国标ID，例如：34020000001320000001',
        rules: [
          { required: true, message: '请输入设备ID' },
          { len: 20, message: '设备ID必须是20位国标ID' },
        ],
      },
      {
        name: 'name',
        label: '设备名称',
        type: 'text',
        required: false,
        placeholder: '设备名称',
      },
      {
        name: 'ip',
        label: 'IP地址',
        type: 'text',
        required: true,
        placeholder: '192.168.1.100',
        rules: [
          { required: true, message: '请输入IP地址' },
          { type: 'string', pattern: /^(\d{1,3}\.){3}\d{1,3}$/, message: '请输入有效的IP地址' },
        ],
      },
      {
        name: 'port',
        label: 'SIP端口',
        type: 'number',
        required: true,
        defaultValue: 5060,
        rules: [
          { required: true, message: '请输入端口' },
          { type: 'number', min: 1, max: 65535, message: '端口范围：1-65535' },
        ],
      },
      {
        name: 'manufacturer',
        label: '制造商',
        type: 'text',
        required: false,
        placeholder: '制造商',
      },
      {
        name: 'model',
        label: '型号',
        type: 'text',
        required: false,
        placeholder: '型号',
      },
      {
        name: 'username',
        label: '用户名（SIP认证）',
        type: 'text',
        required: false,
        placeholder: 'SIP认证用户名（可选）',
      },
      {
        name: 'password',
        label: '密码（SIP认证）',
        type: 'password',
        required: false,
        placeholder: 'SIP认证密码（可选）',
      },
    ],
  };

  async discover(options: DiscoveryOptions): Promise<Device[]> {
    // GB28181设备通过手动添加或SIP注册，不支持自动发现
    return this.getDevices();
  }

  async getDevices(): Promise<Device[]> {
    const response = await apiClient.get<{ devices: any[]; count: number }>('/gb28181/devices');
    // 转换后端格式到前端格式
    const devices = response.devices || [];
    return devices.map(device => ({
      device_id: device.id || device.device_id,
      name: device.name || device.id || device.device_id,
      ip: device.ip,
      port: device.port,
      manufacturer: device.manufacturer,
      model: device.model,
      protocol: 'gb28181' as DeviceProtocol,
      online: device.online || false,
      channels: device.channels || [],
      username: device.username,
      password: device.password,
    }));
  }

  async getDevice(deviceId: string): Promise<Device> {
    const device = await apiClient.get<any>(`/gb28181/devices/${deviceId}`);
    return {
      device_id: device.id || device.device_id,
      name: device.name || device.id || device.device_id,
      ip: device.ip,
      port: device.port,
      manufacturer: device.manufacturer,
      model: device.model,
      protocol: 'gb28181' as DeviceProtocol,
      online: device.online || false,
      channels: device.channels || [],
      username: device.username,
      password: device.password,
    };
  }

  async addDevice(device: Partial<Device>): Promise<Device> {
    const response = await apiClient.post<{ device_id: string; id: string; ip: string }>('/gb28181/devices/add', device);
    return this.getDevice(response.device_id || response.id);
  }

  async deleteDevice(deviceId: string): Promise<void> {
    return apiClient.delete<void>(`/gb28181/devices/${deviceId}`);
  }

  async startDeviceStream(
    deviceId: string,
    app: string,
    stream: string,
    options?: { channelId?: string; outputProtocol?: Protocol }
  ): Promise<void> {
    const channelId = options?.channelId || deviceId;
    const outputProtocol = options?.outputProtocol || 'http-flv';
    return apiClient.post<void>('/streams/start', {
      protocol: 'gb28181',
      source_url: `gb28181://${deviceId}/${channelId}`,
      app,
      stream,
      output_protocol: outputProtocol,
    });
  }
}

