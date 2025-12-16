import { useMemo } from 'react';
import { Card } from 'antd';
import EChartsWrapper from './EChartsWrapper';
import type { SystemStats } from '@/api/monitoring';

interface ResourceChartProps {
  data: SystemStats;
  loading?: boolean;
}

export default function ResourceChart({ data, loading = false }: ResourceChartProps) {
  if (!data) return <Card title="系统资源" loading={loading} />;

  const cpuUsage = data.system_cpu_usage || 0;
  const memoryUsage = ((data.system_memory_usage || 0) / (data.system_total_memory || 1)) * 100;

  const option = useMemo(() => ({
    tooltip: {
      trigger: 'axis',
      axisPointer: {
        type: 'shadow',
      },
    },
    legend: {
      data: ['CPU使用率', '内存使用率'],
    },
    grid: {
      left: '3%',
      right: '4%',
      bottom: '3%',
      containLabel: true,
    },
    xAxis: {
      type: 'category',
      data: ['系统资源'],
    },
    yAxis: {
      type: 'value',
      max: 100,
      axisLabel: {
        formatter: '{value}%',
      },
    },
    series: [
      {
        name: 'CPU使用率',
        type: 'bar',
        data: [cpuUsage.toFixed(2)],
        itemStyle: {
          color: cpuUsage > 80 ? '#ff4d4f' : cpuUsage > 60 ? '#faad14' : '#52c41a',
        },
      },
      {
        name: '内存使用率',
        type: 'bar',
        data: [memoryUsage.toFixed(2)],
        itemStyle: {
          color: memoryUsage > 80 ? '#ff4d4f' : memoryUsage > 60 ? '#faad14' : '#52c41a',
        },
      },
    ],
  }), [cpuUsage, memoryUsage]);

  return (
    <Card title="系统资源" loading={loading}>
      <EChartsWrapper
        option={option as any}
        style={{ height: '300px' }}
        opts={{
          renderer: 'canvas',
        }}
        notMerge={true}
        lazyUpdate={true}
      />
    </Card>
  );
}

