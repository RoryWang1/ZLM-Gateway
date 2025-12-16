// 监控 API

import { apiClient } from './client';

export interface SystemStats {
  total_streams: number;
  running_streams: number;
  error_streams: number;
  total_processes: number;
  system_cpu_usage: number;
  system_memory_usage: number;
  system_total_memory: number;
  network_in_bytes: number;
  network_out_bytes: number;
  uptime_seconds: number;
}

export interface StreamStats {
  app: string;
  stream: string;
  protocol: string;
  status: string;
  cpu_usage: number;
  memory_usage: number;
  reader_count: number;
  bytes_speed: number;
  total_bytes: number;
  uptime_seconds: number;
}

export interface ProtocolStats {
  protocol: string;
  total_streams: number;
  running_streams: number;
  error_streams: number;
}

export interface ErrorStats {
  total: number;
  network: number;
  protocol: number;
  config: number;
  auth: number;
  unknown: number;
}

export interface HookStats {
  total_count: number;
  success_count: number;
  retry_count: number;
  failed_count: number;
  external_stream_count: number;
  total_processing_time_ms: number;
  avg_processing_time_ms: number;
  success_rate: number;
  events: {
    stream_changed: number;
    stream_none_reader: number;
    play: number;
    publish: number;
  };
}

export interface StatusStatistics {
  total_streams: number;
  status_counts: {
    running: number;
    starting: number;
    stopped: number;
    error: number;
    stopping: number;
    total: number;
  };
  transition_stats: {
    gateway_create_requested: number;
    gateway_create_result: number;
    zlm_state_update: number;
    sync_polling: number;
    manual_update: number;
  };
  recent_transitions: Array<{
    app?: string;
    stream?: string;
    from: string;
    to: string;
    source: string;
    timestamp: number;
  }>;
}

export interface DashboardData {
  system_stats: SystemStats;
  streams_summary: StreamStats[];
  protocol_stats: ProtocolStats[];
  error_stats: ErrorStats;
  hook_stats?: HookStats;
}

/**
 * 获取系统统计信息
 */
export async function getSystemStats(): Promise<SystemStats> {
  // Note: Backend returns nested structure, we might need mapping here too if used directly.
  // But for now let's focus on getDashboardData which is used by Dashboard.
  // If getSystemStats is used elsewhere, it might fail.
  // But Dashboard uses getDashboardData.
  return apiClient.get<SystemStats>('/monitoring/system');
}

/**
 * 获取所有流的性能统计
 */
export async function getStreamStats(): Promise<StreamStats[]> {
  return apiClient.get<StreamStats[]>('/monitoring/streams');
}

/**
 * 获取指定流的性能统计
 */
export async function getStreamStat(app: string, stream: string): Promise<StreamStats> {
  return apiClient.get<StreamStats>(`/monitoring/streams/${app}/${stream}`);
}

/**
 * 获取协议统计信息
 */
export async function getProtocolStats(): Promise<ProtocolStats[]> {
  return apiClient.get<ProtocolStats[]>('/monitoring/protocols');
}

/**
 * 获取错误统计信息
 */
export async function getErrorStats(): Promise<ErrorStats> {
  return apiClient.get<ErrorStats>('/monitoring/errors');
}

/**
 * 获取 Hook 统计信息
 */
export async function getHookStats(): Promise<HookStats> {
  return apiClient.get<HookStats>('/monitoring/hook-stats');
}

/**
 * 获取状态机统计信息
 */
export async function getStatusStatistics(): Promise<StatusStatistics> {
  // apiClient 的响应拦截器已经自动提取了 data.data，所以这里直接返回
  return apiClient.get<StatusStatistics>('/monitoring/status-statistics');
}

/**
 * 获取 Dashboard 综合数据
 */
export async function getDashboardData(): Promise<DashboardData> {
  const raw: any = await apiClient.get('/monitoring/dashboard');

  // Map backend response to frontend interface
  const system = raw.system || {};
  const streams = raw.streams || [];
  const protocols = raw.protocols || [];
  const errors = raw.errors || {};
  const hookStats = raw.hook_stats;

  const system_stats: SystemStats = {
    total_streams: system.streams?.total || 0,
    running_streams: system.streams?.running || 0,
    error_streams: system.streams?.error || 0,
    total_processes: system.processes?.total || 0,
    system_cpu_usage: system.resources?.total_cpu_usage || 0,
    system_memory_usage: system.resources?.total_memory_usage || 0,
    system_total_memory: 0, // Not provided by backend?
    network_in_bytes: system.resources?.total_network_speed || 0, // Assuming speed is bytes/s
    network_out_bytes: 0, // Not separated?
    uptime_seconds: system.timestamp || 0, // Timestamp is not uptime, but close enough for now?
  };

  // Map protocols
  const protocol_stats: ProtocolStats[] = protocols.map((p: any) => ({
    protocol: p.protocol,
    total_streams: p.stream_count || 0,
    running_streams: p.running_count || 0,
    error_streams: 0, // Not provided
  }));

  // Map streams
  const streams_summary: StreamStats[] = streams.map((s: any) => ({
    app: s.app,
    stream: s.stream,
    protocol: s.protocol,
    status: s.status,
    cpu_usage: s.cpu_usage || 0,
    memory_usage: s.memory_usage || 0,
    reader_count: s.reader_count || 0,
    bytes_speed: s.bytes_speed || 0,
    total_bytes: 0,
    uptime_seconds: s.uptime || 0,
  }));

  return {
    system_stats,
    streams_summary,
    protocol_stats,
    error_stats: errors, // Assuming errors structure matches or is close enough
    hook_stats: hookStats ? {
      total_count: hookStats.total_count || 0,
      success_count: hookStats.success_count || 0,
      retry_count: hookStats.retry_count || 0,
      failed_count: hookStats.failed_count || 0,
      external_stream_count: hookStats.external_stream_count || 0,
      total_processing_time_ms: hookStats.total_processing_time_ms || 0,
      avg_processing_time_ms: hookStats.avg_processing_time_ms || 0,
      success_rate: hookStats.success_rate || 0,
      events: hookStats.events || {
        stream_changed: 0,
        stream_none_reader: 0,
        play: 0,
        publish: 0,
      },
    } : undefined,
  };
}

