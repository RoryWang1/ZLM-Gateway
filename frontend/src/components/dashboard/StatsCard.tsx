import { Card, Statistic } from 'antd';
import { ArrowUpOutlined, ArrowDownOutlined } from '@ant-design/icons';

interface StatsCardProps {
  title: string;
  value: number | string;
  prefix?: React.ReactNode;
  suffix?: React.ReactNode;
  trend?: 'up' | 'down';
  trendValue?: string;
  loading?: boolean;
}

export default function StatsCard({
  title,
  value,
  prefix,
  suffix,
  trend,
  trendValue,
  loading = false,
}: StatsCardProps) {
  return (
    <Card loading={loading}>
      <Statistic
        title={title}
        value={value}
        prefix={prefix}
        suffix={suffix}
        valueStyle={{ color: trend === 'up' ? '#3f8600' : trend === 'down' ? '#cf1322' : undefined }}
      />
      {trend && trendValue && (
        <div style={{ marginTop: 8, fontSize: 12, color: '#8c8c8c' }}>
          {trend === 'up' ? <ArrowUpOutlined /> : <ArrowDownOutlined />} {trendValue}
        </div>
      )}
    </Card>
  );
}

