// 健康检查 API

import { apiClient } from './client';

export interface HealthStatus {
  status: string;
  zlmediakit_online?: boolean;
}

/**
 * 健康检查
 */
export async function checkHealth(): Promise<HealthStatus> {
  return apiClient.get<HealthStatus>('/health', { baseURL: '' });
}
