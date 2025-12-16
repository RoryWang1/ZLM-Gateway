import { useMemo } from 'react';
import { Card } from 'antd';
import EChartsWrapper from './EChartsWrapper';
import type { ProtocolStats } from '@/api/monitoring';

interface ProtocolChartProps {
  data: ProtocolStats[];
  loading?: boolean;
}

export default function ProtocolChart({ data, loading = false }: ProtocolChartProps) {
  const option = useMemo(() => ({
    tooltip: {
      trigger: 'item',
    },
    legend: {
      orient: 'vertical',
      left: 'left',
    },
    series: [
      {
        name: '协议分布',
        type: 'pie',
        radius: '50%',
        data: data.map((item) => ({
          value: item.total_streams,
          name: item.protocol.toUpperCase(),
        })),
        emphasis: {
          itemStyle: {
            shadowBlur: 10,
            shadowOffsetX: 0,
            shadowColor: 'rgba(0, 0, 0, 0.5)',
          },
        },
      },
    ],
  }), [data]);

  return (
    <Card title="协议分布" loading={loading}>
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

