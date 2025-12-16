// 设备发现 API

import { apiClient, discoveryClient } from './client';
import type { Device, DeviceStream, DeviceProtocol } from '@/types/device';

/**
 * 发现 ONVIF 设备
 */
export async function discoverONVIF(timeoutSeconds?: number): Promise<Device[]> {
  const response = await discoveryClient.post<{ devices: Device[]; count: number }>('/devices/discover', {
    timeout_seconds: timeoutSeconds || 5,
  });
  return response.devices || [];
}

/**
 * 获取所有 ONVIF 设备
 */
export async function getONVIFDevices(): Promise<Device[]> {
  const response = await apiClient.get<{ devices: Device[]; count: number }>('/devices');
  return response.devices || [];
}

/**
 * 获取指定 ONVIF 设备信息
 */
export async function getONVIFDevice(deviceId: string): Promise<Device> {
  return apiClient.get<Device>(`/devices/${deviceId}`);
}

/**
 * 获取 ONVIF 设备的流地址
 */
export async function getONVIFDeviceStreams(deviceId: string): Promise<DeviceStream[]> {
  return apiClient.get<DeviceStream[]>(`/devices/${deviceId}/rtsp-urls`);
}

/**
 * 启动 ONVIF 设备的流
 */
export async function startONVIFDeviceStream(
  deviceId: string,
  app: string,
  stream: string
): Promise<void> {
  return apiClient.post<void>(`/devices/${deviceId}/streams/start`, {
    app,
    stream,
  });
}

/**
 * 发现 ISAPI 设备
 */
export async function discoverISAPI(ipRange: string, port?: number): Promise<Device[]> {
  const response = await discoveryClient.post<{ devices: Device[]; count: number }>('/isapi/devices/discover', {
    ip_range: ipRange,
    port: port || 80,
  });
  return response.devices || [];
}

/**
 * 获取所有 ISAPI 设备
 */
export async function getISAPIDevices(): Promise<Device[]> {
  const response = await apiClient.get<{ devices: Device[]; count: number }>('/isapi/devices');
  return response.devices || [];
}

/**
 * 获取指定 ISAPI 设备信息
 */
export async function getISAPIDevice(deviceId: string): Promise<Device> {
  return apiClient.get<Device>(`/isapi/devices/${deviceId}`);
}

/**
 * 获取 ISAPI 设备的流地址
 */
export async function getISAPIDeviceStreams(deviceId: string): Promise<DeviceStream[]> {
  return apiClient.get<DeviceStream[]>(`/isapi/devices/${deviceId}/rtsp-urls`);
}

/**
 * 启动 ISAPI 设备的流
 */
export async function startISAPIDeviceStream(
  deviceId: string,
  app: string,
  stream: string
): Promise<void> {
  return apiClient.post<void>(`/isapi/devices/${deviceId}/streams/start`, {
    app,
    stream,
  });
}

/**
 * 发现大华设备
 */
export async function discoverDahua(
  ipRange: string,
  username: string,
  password: string,
  port?: number
): Promise<Device[]> {
  const response = await discoveryClient.post<{ devices: Device[]; count: number }>('/dahua/devices/discover', {
    ip_range: ipRange,
    port: port || 80,
    username,
    password,
  });
  return response.devices || [];
}

/**
 * 获取所有大华设备
 */
export async function getDahuaDevices(): Promise<Device[]> {
  const response = await apiClient.get<{ devices: Device[]; count: number }>('/dahua/devices');
  return response.devices || [];
}

/**
 * 获取指定大华设备信息
 */
export async function getDahuaDevice(deviceId: string): Promise<Device> {
  return apiClient.get<Device>(`/dahua/devices/${deviceId}`);
}

/**
 * 获取大华设备的流地址
 */
export async function getDahuaDeviceStreams(deviceId: string): Promise<DeviceStream[]> {
  return apiClient.get<DeviceStream[]>(`/dahua/devices/${deviceId}/rtsp-urls`);
}

/**
 * 启动大华设备的流
 */
export async function startDahuaDeviceStream(
  deviceId: string,
  app: string,
  stream: string
): Promise<void> {
  return apiClient.post<void>(`/dahua/devices/${deviceId}/streams/start`, {
    app,
    stream,
  });
}

/**
 * 发现 PSIA 设备
 */
export async function discoverPSIA(
  ipRange: string,
  username: string,
  password: string,
  port?: number
): Promise<Device[]> {
  const response = await discoveryClient.post<{ devices: Device[]; count: number }>('/psia/devices/discover', {
    ip_range: ipRange,
    port: port || 80,
    username,
    password,
  });
  return response.devices || [];
}

/**
 * 获取所有 PSIA 设备
 */
export async function getPSIADevices(): Promise<Device[]> {
  const response = await apiClient.get<{ devices: Device[]; count: number }>('/psia/devices');
  return response.devices || [];
}

/**
 * 获取指定 PSIA 设备信息
 */
export async function getPSIADevice(deviceId: string): Promise<Device> {
  return apiClient.get<Device>(`/psia/devices/${deviceId}`);
}

/**
 * 获取 PSIA 设备的流地址
 */
export async function getPSIADeviceStreams(deviceId: string): Promise<DeviceStream[]> {
  return apiClient.get<DeviceStream[]>(`/psia/devices/${deviceId}/rtsp-urls`);
}

/**
 * 启动 PSIA 设备的流
 */
export async function startPSIADeviceStream(
  deviceId: string,
  app: string,
  stream: string
): Promise<void> {
  return apiClient.post<void>(`/psia/devices/${deviceId}/streams/start`, {
    app,
    stream,
  });
}

/**
 * 发现本地摄像头设备
 */
export async function discoverLocalCameras(): Promise<Device[]> {
  const response = await apiClient.post<{ devices: Device[]; count: number }>('/local-camera/devices/discover', {});
  return response.devices || [];
}

/**
 * 获取所有本地摄像头设备
 */
export async function getLocalCameras(): Promise<Device[]> {
  const response = await apiClient.get<{ devices: Device[]; count: number }>('/local-camera/devices');
  return response.devices || [];
}

/**
 * 刷新本地摄像头设备（强制重新扫描）
 */
export async function refreshLocalCameras(): Promise<Device[]> {
  const response = await apiClient.post<{ devices: Device[]; count: number }>('/local-camera/devices/refresh', {});
  return response.devices || [];
}

/**
 * 获取指定本地摄像头设备信息
 */
export async function getLocalCamera(deviceId: string): Promise<Device> {
  return apiClient.get<Device>(`/local-camera/devices/${deviceId}`);
}

/**
 * 启动本地摄像头流
 */
export async function startLocalCameraStream(
  deviceId: string,
  app: string,
  stream: string,
  // 本地摄像头默认使用 WebRTC 以获得更低延迟
  outputProtocol: string = 'webrtc'
): Promise<void> {
  return apiClient.post<void>('/local-camera/streams/start', {
    device_id: deviceId,
    app,
    stream,
    output_protocol: outputProtocol,
  });
}

/**
 * 停止本地摄像头流
 */
export async function stopLocalCameraStream(app: string, stream: string): Promise<void> {
  return apiClient.post<void>('/local-camera/streams/stop', {
    app,
    stream,
  });
}

/**
 * 获取所有 GB28181 设备
 */
export async function getGB28181Devices(): Promise<Device[]> {
  const response = await apiClient.get<{ devices: any[]; count: number }>('/gb28181/devices');
  // apiClient 的响应拦截器已经返回了 data.data，所以 response 直接就是 { devices: [...], count: ... }
  // 转换后端格式到前端格式
  const devices = response.devices || [];
  const mappedDevices = devices.map(device => ({
    device_id: device.id || device.device_id,
    name: device.name || device.id || device.device_id,
    ip: device.ip,
    port: device.port,
    manufacturer: device.manufacturer,
    model: device.model,
    protocol: 'gb28181' as DeviceProtocol,
    online: device.online || false,
    channels: device.channels || [],
  }));
  console.log('getGB28181Devices result:', mappedDevices);
  return mappedDevices;
}

/**
 * 获取指定 GB28181 设备信息
 */
export async function getGB28181Device(deviceId: string): Promise<Device> {
  return apiClient.get<Device>(`/gb28181/devices/${deviceId}`);
}

/**
 * 添加 GB28181 设备
 */
export async function addGB28181Device(device: Partial<Device>): Promise<Device> {
  const response = await apiClient.post<{ device_id: string; id: string; ip: string }>('/gb28181/devices/add', device);
  // 返回添加后的设备信息
  return getGB28181Device(response.device_id || response.id);
}

/**
 * 删除 GB28181 设备
 */
export async function deleteGB28181Device(deviceId: string): Promise<void> {
  return apiClient.delete<void>(`/gb28181/devices/${deviceId}`);
}

/**
 * 启动 GB28181 设备流
 * GB28181只支持HLS和FLV，不支持WebRTC
 */
export async function startGB28181DeviceStream(
  deviceId: string,
  channelId: string,
  app: string,
  stream: string,
  outputProtocol: string = 'http-flv'
): Promise<void> {
  // GB28181只支持HLS和FLV
  if (outputProtocol !== 'hls' && outputProtocol !== 'http-flv') {
    throw new Error('GB28181只支持HLS和FLV输出协议');
  }
  return apiClient.post<void>('/gb28181/streams/start', {
    device_id: deviceId,
    channel_id: channelId,
    output_protocol: outputProtocol,
  });
}

/**
 * 停止 GB28181 设备流
 */
export async function stopGB28181DeviceStream(
  deviceId: string,
  channelId: string
): Promise<void> {
  return apiClient.post<void>('/gb28181/streams/stop', {
    device_id: deviceId,
    channel_id: channelId,
  });
}

/**
 * 通用设备发现函数（根据协议类型调用对应的发现函数）
 */
export async function discoverDevices(
  protocol: DeviceProtocol,
  options: {
    timeoutSeconds?: number;
    ipRange?: string;
    port?: number;
    username?: string;
    password?: string;
  }
): Promise<Device[]> {
  switch (protocol) {
    case 'onvif':
      return discoverONVIF(options.timeoutSeconds);
    case 'isapi':
      if (!options.ipRange) throw new Error('IP range is required for ISAPI discovery');
      return discoverISAPI(options.ipRange, options.port);
    case 'dahua':
      if (!options.ipRange || !options.username || !options.password) {
        throw new Error('IP range, username and password are required for Dahua discovery');
      }
      return discoverDahua(options.ipRange, options.username, options.password, options.port);
    case 'psia':
      if (!options.ipRange || !options.username || !options.password) {
        throw new Error('IP range, username and password are required for PSIA discovery');
      }
      return discoverPSIA(options.ipRange, options.username, options.password, options.port);
    case 'local-camera':
      return discoverLocalCameras();
    case 'gb28181':
      // GB28181设备通过手动添加，不支持自动发现
      return getGB28181Devices();
    default:
      throw new Error(`Unsupported device protocol: ${protocol}`);
  }
}

