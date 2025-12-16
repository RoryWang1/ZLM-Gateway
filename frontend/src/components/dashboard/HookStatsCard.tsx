// Hook 统计卡片组件

import { Card, Row, Col, Statistic, Progress } from 'antd';
import { HookStats } from '@/api/monitoring';

interface HookStatsCardProps {
  stats?: HookStats;
  loading?: boolean;
}

export default function HookStatsCard({ stats, loading }: HookStatsCardProps) {
  if (loading || !stats) {
    return (
      <Card title="Hook 状态同步统计" loading={loading}>
        <div style={{ textAlign: 'center', padding: '20px' }}>
          暂无数据
        </div>
      </Card>
    );
  }

  const successRate = stats.success_rate || 0;
  const avgTime = stats.avg_processing_time_ms || 0;

  return (
    <Card title="Hook 状态同步统计" style={{ marginBottom: 16 }}>
      <Row gutter={[16, 16]}>
        <Col xs={24} sm={12} md={6}>
          <Statistic
            title="总事件数"
            value={stats.total_count}
            valueStyle={{ color: '#1890ff' }}
          />
        </Col>
        <Col xs={24} sm={12} md={6}>
          <Statistic
            title="成功处理"
            value={stats.success_count}
            valueStyle={{ color: '#3f8600' }}
          />
        </Col>
        <Col xs={24} sm={12} md={6}>
          <Statistic
            title="重试次数"
            value={stats.retry_count}
            valueStyle={{ color: '#faad14' }}
          />
        </Col>
        <Col xs={24} sm={12} md={6}>
          <Statistic
            title="失败次数"
            value={stats.failed_count}
            valueStyle={{ color: stats.failed_count > 0 ? '#cf1322' : '#3f8600' }}
          />
        </Col>
        <Col xs={24} sm={12} md={6}>
          <Statistic
            title="外部推流数"
            value={stats.external_stream_count}
            valueStyle={{ color: '#722ed1' }}
          />
        </Col>
        <Col xs={24} sm={12} md={6}>
          <Statistic
            title="平均处理时间"
            value={avgTime.toFixed(2)}
            suffix="ms"
            valueStyle={{ color: avgTime > 100 ? '#faad14' : '#3f8600' }}
          />
        </Col>
        <Col xs={24} sm={12} md={6}>
          <div>
            <div style={{ marginBottom: 8 }}>
              <span>成功率: </span>
              <span style={{ fontWeight: 'bold', color: successRate >= 95 ? '#3f8600' : successRate >= 80 ? '#faad14' : '#cf1322' }}>
                {successRate.toFixed(2)}%
              </span>
            </div>
            <Progress
              percent={successRate}
              status={successRate >= 95 ? 'success' : successRate >= 80 ? 'active' : 'exception'}
              strokeColor={successRate >= 95 ? '#3f8600' : successRate >= 80 ? '#faad14' : '#cf1322'}
            />
          </div>
        </Col>
        <Col xs={24} sm={12} md={6}>
          <Statistic
            title="总处理时间"
            value={(stats.total_processing_time_ms / 1000).toFixed(2)}
            suffix="秒"
            valueStyle={{ color: '#1890ff' }}
          />
        </Col>
      </Row>

      {/* 按事件类型统计 */}
      <div style={{ marginTop: 16, paddingTop: 16, borderTop: '1px solid #f0f0f0' }}>
        <div style={{ marginBottom: 12, fontWeight: 'bold' }}>事件类型统计</div>
        <Row gutter={[16, 16]}>
          <Col xs={24} sm={12} md={6}>
            <Statistic
              title="流状态变化"
              value={stats.events.stream_changed}
              valueStyle={{ color: '#1890ff' }}
            />
          </Col>
          <Col xs={24} sm={12} md={6}>
            <Statistic
              title="无播放器"
              value={stats.events.stream_none_reader}
              valueStyle={{ color: '#faad14' }}
            />
          </Col>
          <Col xs={24} sm={12} md={6}>
            <Statistic
              title="播放事件"
              value={stats.events.play}
              valueStyle={{ color: '#3f8600' }}
            />
          </Col>
          <Col xs={24} sm={12} md={6}>
            <Statistic
              title="推流事件"
              value={stats.events.publish}
              valueStyle={{ color: '#722ed1' }}
            />
          </Col>
        </Row>
      </div>
    </Card>
  );
}

