// 流管理 API

import { apiClient } from './client';
import type { Stream, StartStreamRequest, StopStreamRequest } from '@/types/stream';


/**
 * 获取所有流列表
 */
export async function getStreams(): Promise<Stream[]> {
  return apiClient.get<Stream[]>('/streams');
}

/**
 * 获取指定流的详细信息
 */
export async function getStreamDetail(app: string, stream: string): Promise<Stream> {
  return apiClient.get<Stream>(`/streams/${app}/${stream}`);
}

/**
 * 启动流
 */
export async function startStream(request: StartStreamRequest): Promise<Stream> {
  return apiClient.post<Stream>('/streams/start', request);
}

/**
 * 停止流
 */
export async function stopStream(request: StopStreamRequest): Promise<void> {
  return apiClient.post<void>('/streams/stop', request);
}

/**
 * 删除流
 */
export async function deleteStream(app: string, stream: string): Promise<void> {
  return apiClient.delete<void>(`/streams/${app}/${stream}`);
}

