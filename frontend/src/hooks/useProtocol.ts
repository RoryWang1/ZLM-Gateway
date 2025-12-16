// 协议相关的统一Hooks

import { useQuery, useMutation, useQueryClient } from '@tanstack/react-query';
import { message } from 'antd';
import { protocolRegistry } from '@/protocols';
import type { DeviceProtocol, Device } from '@/types/device';
import type { DiscoveryOptions } from '@/protocols/types';
import type { Protocol } from '@/types/stream';

/**
 * 获取协议适配器
 */
export function useProtocolAdapter(protocol: DeviceProtocol) {
  return protocolRegistry.get(protocol);
}

/**
 * 获取所有已注册的协议
 */
export function useProtocols() {
  return protocolRegistry.getAll();
}

/**
 * 获取设备列表（使用协议适配器）
 */
export function useProtocolDevices(protocol: DeviceProtocol) {
  const adapter = protocolRegistry.get(protocol);

  return useQuery({
    queryKey: ['devices', protocol],
    queryFn: async () => {
      if (!adapter) {
        return [];
      }
      try {
        return await adapter.getDevices();
      } catch (error: any) {
        // 如果后端返回"Gateway not enabled"错误，快速返回空数组，不阻塞页面
        const errorMessage = error?.message || String(error || '');
        if (
          errorMessage.includes('not enabled') ||
          errorMessage.includes('未启用') ||
          error?.response?.status === 500 ||
          error?.response?.status === 404
        ) {
          console.debug(`协议 ${protocol} 未启用，返回空设备列表`);
          return [];
        }
        // 其他错误继续抛出，让React Query处理重试
        throw error;
      }
    },
    enabled: !!adapter,
    staleTime: 10000, // 10秒内数据视为新鲜，不重新请求
    gcTime: 5 * 60 * 1000, // 5分钟后清理缓存（原cacheTime）
    retry: (failureCount, error: any) => {
      // 如果是"Gateway not enabled"错误，不重试
      const errorMessage = error?.message || String(error || '');
      if (
        errorMessage.includes('not enabled') ||
        errorMessage.includes('未启用') ||
        error?.response?.status === 500 ||
        error?.response?.status === 404
      ) {
        return false;
      }
      // 其他错误最多重试1次
      return failureCount < 1;
    },
    retryDelay: 1000, // 重试延迟1秒
    refetchOnWindowFocus: false, // 窗口聚焦时不重新请求
    refetchOnMount: true, // 组件挂载时重新请求
    refetchOnReconnect: true, // 网络重连时重新请求
  });
}

/**
 * 发现设备（使用协议适配器）
 */
export function useProtocolDiscover(protocol: DeviceProtocol) {
  const queryClient = useQueryClient();
  const adapter = protocolRegistry.get(protocol);

  return useMutation({
    mutationFn: (options: DiscoveryOptions) => {
      if (!adapter) {
        throw new Error(`Protocol ${protocol} not found`);
      }
      return adapter.discover(options);
    },
    onSuccess: () => {
      message.success('设备发现成功');
      queryClient.invalidateQueries({ queryKey: ['devices', protocol] });
      queryClient.invalidateQueries({ queryKey: ['devices'] });
    },
    onError: (error: Error) => {
      message.error(`设备发现失败: ${error.message}`);
    },
  });
}

/**
 * 添加设备（使用协议适配器）
 */
export function useProtocolAddDevice(protocol: DeviceProtocol) {
  const queryClient = useQueryClient();
  const adapter = protocolRegistry.get(protocol);

  return useMutation({
    mutationFn: (device: Partial<Device>) => {
      if (!adapter) {
        throw new Error(`Protocol ${protocol} not found`);
      }
      return adapter.addDevice(device);
    },
    onSuccess: () => {
      message.success('设备添加成功');
      queryClient.invalidateQueries({ queryKey: ['devices', protocol] });
      queryClient.invalidateQueries({ queryKey: ['devices'] });
    },
    onError: (error: Error) => {
      message.error(`设备添加失败: ${error.message}`);
    },
  });
}

/**
 * 删除设备（使用协议适配器）
 */
export function useProtocolDeleteDevice(protocol: DeviceProtocol) {
  const queryClient = useQueryClient();
  const adapter = protocolRegistry.get(protocol);

  return useMutation({
    mutationFn: (deviceId: string) => {
      if (!adapter) {
        throw new Error(`Protocol ${protocol} not found`);
      }
      return adapter.deleteDevice(deviceId);
    },
    onSuccess: () => {
      message.success('设备删除成功');
      queryClient.invalidateQueries({ queryKey: ['devices', protocol] });
      queryClient.invalidateQueries({ queryKey: ['devices'] });
    },
    onError: (error: Error) => {
      message.error(`设备删除失败: ${error.message}`);
    },
  });
}

/**
 * 启动设备流（使用协议适配器）
 */
export function useProtocolStartStream(protocol: DeviceProtocol) {
  const queryClient = useQueryClient();
  const adapter = protocolRegistry.get(protocol);

  return useMutation({
    mutationFn: ({
      deviceId,
      app,
      stream,
      channelId,
      outputProtocol,
    }: {
      deviceId: string;
      app: string;
      stream: string;
      channelId?: string;
      outputProtocol?: Protocol;
    }) => {
      if (!adapter) {
        throw new Error(`Protocol ${protocol} not found`);
      }
      return adapter.startDeviceStream(deviceId, app, stream, {
        channelId,
        outputProtocol,
      });
    },
    onSuccess: () => {
      message.success('流启动成功');
      queryClient.invalidateQueries({ queryKey: ['streams'] });
    },
    onError: (error: Error) => {
      message.error(`流启动失败: ${error.message}`);
    },
  });
}

/**
 * 获取设备流地址（使用协议适配器）
 */
export function useProtocolDeviceStreams(protocol: DeviceProtocol, deviceId: string) {
  const adapter = protocolRegistry.get(protocol);

  return useQuery({
    queryKey: ['devices', protocol, deviceId, 'streams'],
    queryFn: () => {
      if (!adapter || !adapter.getDeviceStreams) {
        return Promise.resolve([]);
      }
      return adapter.getDeviceStreams(deviceId);
    },
    enabled: !!adapter && !!deviceId && !!adapter.getDeviceStreams,
  });
}

