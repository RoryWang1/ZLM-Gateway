// 流管理 Hook

import { useQuery, useMutation, useQueryClient, keepPreviousData } from '@tanstack/react-query';
import { message } from 'antd';
import { getStreams, getStreamDetail, startStream, stopStream, deleteStream } from '@/api/streams';

import { useEffect } from 'react';
import { webSocketService } from '../api/websocket';
import { Stream } from '@/types/stream';

/**
 * 获取流列表
 */
export function useStreams() {
  return useQuery({
    queryKey: ['streams'],
    queryFn: getStreams,
    refetchInterval: 30000, // 降低轮询频率，主要依赖 WebSocket
    placeholderData: keepPreviousData,
    gcTime: 2 * 60 * 1000, // 2分钟后清理缓存，防止流数据累积过多
    staleTime: 10000, // 10秒内数据视为新鲜
  });
}

/**
 * 监听流状态更新
 */
export function useStreamUpdates() {
  const queryClient = useQueryClient();

  useEffect(() => {
    const handleUpdate = (message: any) => {
      // 减少日志输出，只在开发环境或错误时输出
      if (import.meta.env.DEV) {
        console.debug('WebSocket Update:', message);
      }

      // 后端发送的消息格式是：{type: "stream_update", data: {...}}
      // 需要从message.data中获取实际的流数据
      if (message.type !== 'stream_update') {
        return; // 忽略非流更新消息
      }

      const data = message.data;
      if (!data) {
        return; // 如果没有data字段，忽略
      }

      queryClient.setQueryData(['streams'], (oldData: Stream[] | undefined) => {
        if (!oldData) return oldData;

        const { app, stream, status, removed } = data;

        // 对于GB28181流，前端列表中的stream可能是带时间戳的（从API获取时返回rtp_stream_id）
        // 后端WebSocket通知可能发送基础名称或带时间戳的名称
        // 需要同时匹配基础名称和带时间戳的名称
        let index = oldData.findIndex(s => s.app === app && s.stream === stream);

        // 如果直接匹配失败，且是GB28181流，进行双向匹配
        if (index === -1 && app === 'gb28181') {
          // 检查stream是否是带时间戳的（格式：xxx_xxx_timestamp）
          const lastUnderscore = stream.lastIndexOf('_');
          let baseStream = stream;
          let isTimestamped = false;

          if (lastUnderscore !== -1 && lastUnderscore < stream.length - 1) {
            const possibleTimestamp = stream.substring(lastUnderscore + 1);
            // 检查是否是时间戳（纯数字，长度>=10）
            if (possibleTimestamp.length >= 10 && /^\d+$/.test(possibleTimestamp)) {
              baseStream = stream.substring(0, lastUnderscore);
              isTimestamped = true;
            }
          }

          // 尝试匹配：如果stream是带时间戳的，尝试匹配基础名称
          if (isTimestamped) {
            index = oldData.findIndex(s => {
              if (s.app !== app) return false;
              // 前端列表中的stream可能是基础名称或带时间戳的名称
              // 如果前端列表中的stream是基础名称，直接匹配
              if (s.stream === baseStream) {
                return true;
              }
              // 如果前端列表中的stream是带时间戳的，检查是否以baseStream开头
              if (s.stream.includes('_') && s.stream.startsWith(baseStream + '_')) {
                return true;
              }
              return false;
            });
          }

          // 如果还是匹配失败，尝试反向匹配：如果stream是基础名称，匹配前端列表中带时间戳的stream
          if (index === -1) {
            index = oldData.findIndex(s => {
              if (s.app !== app) return false;
              // 如果前端列表中的stream是带时间戳的，检查是否以传入的stream（基础名称）开头
              if (s.stream.includes('_') && s.stream.startsWith(stream + '_')) {
                // 验证后面确实是时间戳
                const sLastUnderscore = s.stream.lastIndexOf('_');
                if (sLastUnderscore !== -1 && sLastUnderscore < s.stream.length - 1) {
                  const sTimestamp = s.stream.substring(sLastUnderscore + 1);
                  if (sTimestamp.length >= 10 && /^\d+$/.test(sTimestamp)) {
                    return true;
                  }
                }
              }
              return false;
            });
          }
        }

        if (removed) {
          if (index !== -1) {
            // 删除流
            return oldData.filter((_, i) => i !== index);
          }
          // 如果匹配失败，但仍然收到removed通知，说明流可能已经被删除了
          // 为了安全起见，重新查询列表
          queryClient.invalidateQueries({ queryKey: ['streams'] });
          return oldData;
        }

        if (index !== -1) {
          // 更新现有流
          const newStreams = [...oldData];
          newStreams[index] = {
            ...newStreams[index],
            status: status,
            // 如果有其他字段更新也可以在这里处理
          };
          return newStreams;
        } else {
          // 新流添加 (通常 WebSocket 消息可能不包含完整信息，这里可能需要重新获取列表或部分更新)
          // 如果消息包含完整流信息，可以直接添加。
          // 目前后端只发了状态，所以如果是新流，最好 invalidate query
          queryClient.invalidateQueries({ queryKey: ['streams'] });
          return oldData;
        }
      });
    };

    // addListener 会自动处理连接（如果还没有连接）
    webSocketService.addListener(handleUpdate);

    // 监听重新连接事件，重新获取完整列表以防由于断开连接而遗漏更新
    const handleReconnect = () => {
      console.log('WebSocket reconnected, invalidating stream queries...');
      queryClient.invalidateQueries({ queryKey: ['streams'] });
    };
    webSocketService.addConnectionListener(handleReconnect);

    return () => {
      // removeListener 会自动处理断开（如果没有其他监听器了）
      webSocketService.removeListener(handleUpdate);
      webSocketService.removeConnectionListener(handleReconnect);
    };
  }, [queryClient]);
}

/**
 * 获取流详情
 */
export function useStreamDetail(app: string, stream: string) {
  return useQuery({
    queryKey: ['streams', app, stream],
    queryFn: () => getStreamDetail(app, stream),
    enabled: !!app && !!stream,
  });
}

/**
 * 启动流
 */
export function useStartStream() {
  const queryClient = useQueryClient();

  return useMutation({
    mutationFn: startStream,
    onSuccess: () => {
      // 提示请求已接收，不等待流完全启动
      message.success('流启动请求已接收，正在启动中...');
      // WebSocket 会推送更新，但为了保险也可以 invalidate
      queryClient.invalidateQueries({ queryKey: ['streams'] });
    },
    onError: (error: Error) => {
      message.error(`流启动请求失败: ${error.message}`);
    },
  });
}

/**
 * 停止流
 */
export function useStopStream() {
  const queryClient = useQueryClient();

  return useMutation({
    mutationFn: stopStream,
    onSuccess: () => {
      message.success('流停止成功');
      queryClient.invalidateQueries({ queryKey: ['streams'] });
    },
    onError: (error: Error) => {
      message.error(`流停止失败: ${error.message}`);
    },
  });
}

/**
 * 删除流
 */
export function useDeleteStream() {
  const queryClient = useQueryClient();

  return useMutation({
    mutationFn: ({ app, stream }: { app: string; stream: string }) => deleteStream(app, stream),
    onSuccess: () => {
      queryClient.invalidateQueries({ queryKey: ['streams'] });
      message.success('流删除成功');
      // TEMPORARILY DISABLED to verify WebSocket updates work independently
      // queryClient.invalidateQueries({ queryKey: ['streams'] });
      console.log('[TEST] Delete mutation onSuccess - NOT invalidating queries, relying on WebSocket');
    },
    onError: (error: Error) => {
      message.error(`流删除失败: ${error.message}`);
    },
  });
}
