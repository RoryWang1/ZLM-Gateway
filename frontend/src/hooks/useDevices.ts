// 设备管理 Hook

import { useQuery, useMutation, useQueryClient } from '@tanstack/react-query';
import { useEffect } from 'react';
import { message } from 'antd';
import { webSocketService } from '@/api/websocket';
import {
  getONVIFDevices,
  discoverONVIF,
  getONVIFDeviceStreams,
  startONVIFDeviceStream,
  discoverDevices,
  getLocalCameras,
  discoverLocalCameras,
  startLocalCameraStream,
  refreshLocalCameras,
  getGB28181Devices,
  addGB28181Device,
  deleteGB28181Device,
  startGB28181DeviceStream,
} from '@/api/devices';
import type { DeviceProtocol, Device } from '@/types/device';

/**
 * 获取 ONVIF 设备列表
 */
export function useONVIFDevices() {
  return useQuery({
    queryKey: ['devices', 'onvif'],
    queryFn: getONVIFDevices,
  });
}

/**
 * 发现 ONVIF 设备
 */
export function useDiscoverONVIF() {
  const queryClient = useQueryClient();

  return useMutation({
    mutationFn: (timeoutSeconds?: number) => discoverONVIF(timeoutSeconds),
    onSuccess: () => {
      message.success('设备发现成功');
      queryClient.invalidateQueries({ queryKey: ['devices'] });
    },
    onError: (error: Error) => {
      message.error(`设备发现失败: ${error.message}`);
    },
  });
}

/**
 * 获取设备流地址
 */
export function useDeviceStreams(protocol: DeviceProtocol, deviceId: string) {
  return useQuery({
    queryKey: ['devices', protocol, deviceId, 'streams'],
    queryFn: () => {
      // 根据协议类型调用对应的 API
      if (protocol === 'onvif') {
        return getONVIFDeviceStreams(deviceId);
      }
      // 其他协议类似处理
      return Promise.resolve([]);
    },
    enabled: !!deviceId,
  });
}

/**
 * 获取本地摄像头设备列表
 */
export function useLocalCameras() {
  return useQuery({
    queryKey: ['devices', 'local-camera'],
    queryFn: getLocalCameras,
  });
}

/**
 * 发现本地摄像头设备
 */
export function useDiscoverLocalCameras() {
  const queryClient = useQueryClient();

  return useMutation({
    mutationFn: () => discoverLocalCameras(),
    onSuccess: () => {
      message.success('本地摄像头发现成功');
      queryClient.invalidateQueries({ queryKey: ['devices', 'local-camera'] });
    },
    onError: (error: Error) => {
      message.error(`本地摄像头发现失败: ${error.message}`);
    },
  });
}

/**
 * 手动刷新本地摄像头设备列表（强制重新扫描）
 */
export function useRefreshLocalCameras() {
  const queryClient = useQueryClient();

  return useMutation({
    mutationFn: () => refreshLocalCameras(),
    onSuccess: () => {
      message.success('本地摄像头已刷新');
      // 刷新本地摄像头列表
      queryClient.invalidateQueries({ queryKey: ['devices', 'local-camera'] });
    },
    onError: (error: Error) => {
      message.error(`刷新本地摄像头失败: ${error.message}`);
    },
  });
}

/**
 * 启动设备流
 */
export function useStartDeviceStream(protocol: DeviceProtocol) {
  const queryClient = useQueryClient();

  return useMutation({
    mutationFn: ({ 
      deviceId, 
      app, 
      stream,
      channelId,
      // 本地摄像头默认使用 WebRTC
      outputProtocol = 'webrtc'
    }: { 
      deviceId: string; 
      app: string; 
      stream: string;
      channelId?: string;
      outputProtocol?: string;
    }) => {
      if (protocol === 'onvif') {
        return startONVIFDeviceStream(deviceId, app, stream);
      } else if (protocol === 'local-camera') {
        return startLocalCameraStream(deviceId, app, stream, outputProtocol);
      } else if (protocol === 'gb28181') {
        // GB28181使用设备ID作为通道ID（如果未指定）
        return startGB28181DeviceStream(deviceId, channelId || deviceId, app, stream, outputProtocol);
      }
      // 其他协议类似处理
      return Promise.resolve();
    },
    onSuccess: () => {
      message.success('设备流启动成功');
      queryClient.invalidateQueries({ queryKey: ['streams'] });
    },
    onError: (error: Error) => {
      message.error(`设备流启动失败: ${error.message}`);
    },
  });
}

/**
 * 通用设备发现
 */
export function useDiscoverDevices() {
  const queryClient = useQueryClient();

  return useMutation({
    mutationFn: ({
      protocol,
      options,
    }: {
      protocol: DeviceProtocol;
      options: {
        timeoutSeconds?: number;
        ipRange?: string;
        port?: number;
        username?: string;
        password?: string;
      };
    }) => discoverDevices(protocol, options),
    onSuccess: () => {
      message.success('设备发现成功');
      queryClient.invalidateQueries({ queryKey: ['devices'] });
    },
    onError: (error: Error) => {
      message.error(`设备发现失败: ${error.message}`);
    },
  });
}

/**
 * 获取 GB28181 设备列表
 */
export function useGB28181Devices() {
  return useQuery({
    queryKey: ['devices', 'gb28181'],
    queryFn: getGB28181Devices,
  });
}

/**
 * 添加 GB28181 设备
 */
export function useAddGB28181Device() {
  const queryClient = useQueryClient();

  return useMutation({
    mutationFn: (device: Partial<Device>) => addGB28181Device(device),
    onSuccess: () => {
      message.success('GB28181设备添加成功');
      queryClient.invalidateQueries({ queryKey: ['devices', 'gb28181'] });
    },
    onError: (error: Error) => {
      message.error(`GB28181设备添加失败: ${error.message}`);
    },
  });
}

/**
 * 删除 GB28181 设备
 */
export function useDeleteGB28181Device() {
  const queryClient = useQueryClient();

  return useMutation({
    mutationFn: (deviceId: string) => deleteGB28181Device(deviceId),
    onSuccess: () => {
      message.success('GB28181设备删除成功');
      queryClient.invalidateQueries({ queryKey: ['devices', 'gb28181'] });
    },
    onError: (error: Error) => {
      message.error(`GB28181设备删除失败: ${error.message}`);
    },
  });
}

/**
 * 监听设备更新（通过 WebSocket）
 */
export function useDeviceUpdates() {
  const queryClient = useQueryClient();

  useEffect(() => {
    const handleUpdate = (message: any) => {
      // 只处理设备更新消息
      if (message.type !== 'device_update') {
        return;
      }

      const { protocol, action } = message.data;

      // 处理本地摄像头设备
      if (protocol === 'local-camera') {
        if (action === 'added' || action === 'removed') {
          queryClient.invalidateQueries({ queryKey: ['devices', 'local-camera'] });
        }
      }
      
      // 处理GB28181设备
      if (protocol === 'gb28181') {
        if (action === 'added' || action === 'removed') {
          queryClient.invalidateQueries({ queryKey: ['devices', 'gb28181'] });
        }
      }
    };

    webSocketService.addListener(handleUpdate);

    return () => {
      webSocketService.removeListener(handleUpdate);
    };
  }, [queryClient]);
}

