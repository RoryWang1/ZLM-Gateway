// 状态机可视化组件

import { Card, Row, Col, Statistic, Table, Tag, Timeline, Empty, Tabs, Skeleton, Progress } from 'antd';
import {
  VideoCameraOutlined,
  PlayCircleOutlined,
  SyncOutlined,
  ExclamationCircleOutlined,
  StopOutlined
} from '@ant-design/icons';
import { StatusStatistics } from '@/api/monitoring';
import { useMemo } from 'react';
import type { ColumnsType } from 'antd/es/table';

interface StateMachineVisualizationProps {
  stats?: StatusStatistics;
  loading?: boolean;
}

const STATUS_COLORS: Record<string, string> = {
  Running: '#3f8600',
  Starting: '#1890ff',
  Stopped: '#8c8c8c',
  Error: '#cf1322',
  Stopping: '#faad14',
};

const SOURCE_COLORS: Record<string, string> = {
  gateway_create_requested: '#1890ff',
  gateway_create_result: '#52c41a',
  zlm_state_update: '#722ed1',
  sync_polling: '#fa8c16',
  manual_update: '#eb2f96',
};

const SOURCE_LABELS: Record<string, string> = {
  gateway_create_requested: 'Gateway 创建请求',
  gateway_create_result: 'Gateway 创建结果',
  zlm_state_update: 'ZLM Hook 更新',
  sync_polling: '轮询同步',
  manual_update: '运维通道',
};

export default function StateMachineVisualization({ stats, loading }: StateMachineVisualizationProps) {
  const statusCounts = stats?.status_counts;
  const recentTransitions = stats?.recent_transitions || [];

  // 为每条记录添加唯一索引，确保 rowKey 唯一性
  const transitionsWithIndex = recentTransitions.map((transition, index) => ({
    ...transition,
    _index: index, // 添加唯一索引
  }));

  // 格式化时间戳
  const formatTimestamp = (timestamp: number) => {
    const date = new Date(timestamp * 1000);
    return date.toLocaleString();
  };

  const tableColumns: ColumnsType<any> = [
    {
      title: '时间',
      dataIndex: 'timestamp',
      key: 'timestamp',
      width: 180,
      render: (ts) => formatTimestamp(ts),
    },
    {
      title: '流名称',
      key: 'stream',
      render: (_, record) => record.app && record.stream ? `${record.app}/${record.stream}` : '-',
    },
    {
      title: '来源',
      dataIndex: 'source',
      key: 'source',
      render: (source) => (
        <Tag color={SOURCE_COLORS[source] || '#8c8c8c'}>
          {SOURCE_LABELS[source] || source}
        </Tag>
      ),
    },
    {
      title: '状态变更',
      key: 'status',
      render: (_, record) => (
        <span>
          <Tag color={STATUS_COLORS[record.from] || '#8c8c8c'}>{record.from}</Tag>
          →
          <Tag color={STATUS_COLORS[record.to] || '#8c8c8c'} style={{ marginLeft: 8 }}>{record.to}</Tag>
        </span>
      ),
    },
  ];

  // 准备时间线数据（最近10条）
  const timelineItems = useMemo(() => {
    return transitionsWithIndex
      .slice(-10)
      .reverse()
      .map((transition, index) => {
        // 显示流名称
        const streamLabel = transition.app && transition.stream
          ? `${transition.app}/${transition.stream}`
          : '流状态转换';

        return {
          key: index,
          color: STATUS_COLORS[transition.to] || '#8c8c8c',
          children: (
            <div>
              <div style={{ fontWeight: 'bold', marginBottom: 4 }}>
                {streamLabel}
              </div>
              <div>
                <Tag color={STATUS_COLORS[transition.from] || '#8c8c8c'} style={{ marginRight: 8 }}>
                  {transition.from}
                </Tag>
                <span style={{ margin: '0 8px' }}>→</span>
                <Tag color={STATUS_COLORS[transition.to] || '#8c8c8c'} style={{ marginRight: 8 }}>
                  {transition.to}
                </Tag>
              </div>
              <div style={{ marginTop: 4, fontSize: 12, color: '#8c8c8c' }}>
                <Tag color={SOURCE_COLORS[transition.source] || '#8c8c8c'} style={{ marginRight: 4 }}>
                  {SOURCE_LABELS[transition.source] || transition.source}
                </Tag>
                {formatTimestamp(transition.timestamp)}
              </div>
            </div>
          ),
        };
      });
  }, [recentTransitions]);

  if (loading || !stats) {
    return (
      <Card title="状态机监控" loading={true} style={{ marginBottom: 24 }}>
        <Skeleton active />
      </Card>
    );
  }

  // 准备Tabs项
  const tabItems = [
    {
      key: 'timeline',
      label: '时间线视图',
      children: (
        <div style={{ height: '400px', overflowY: 'auto', padding: '16px' }}>
          {timelineItems.length > 0 ? (
            <Timeline items={timelineItems} />
          ) : (
            <Empty description="暂无状态转换记录" />
          )}
        </div>
      )
    },
    {
      key: 'table',
      label: '表格视图',
      children: (
        <Table
          columns={tableColumns}
          dataSource={transitionsWithIndex.slice().reverse()}
          rowKey={(record) => `${record.timestamp}-${record.from}-${record.to}-${record.source}-${record._index}`}
          pagination={{ pageSize: 10 }}
          size="small"
          scroll={{ y: 300 }}
        />
      )
    }
  ];

  return (
    <Card title="流状态监控" style={{ marginBottom: 24 }}>
      {/* 状态概览 */}
      <div style={{ marginBottom: 24 }}>
        <Row gutter={[16, 16]}>
          <Col span={3}>
            <Statistic title="总流数" value={stats.total_streams} prefix={<VideoCameraOutlined />} />
          </Col>
          <Col span={3}>
            <Statistic
              title="运行中"
              value={stats.status_counts.running || 0}
              valueStyle={{ color: '#52c41a' }}
              prefix={<PlayCircleOutlined />}
            />
          </Col>
          <Col span={3}>
            <Statistic
              title="启动中"
              value={stats.status_counts.starting || 0}
              valueStyle={{ color: '#1890ff' }}
              prefix={<SyncOutlined spin />}
            />
          </Col>
          <Col span={3}>
            <Statistic
              title="错误"
              value={stats.status_counts.error || 0}
              valueStyle={{ color: '#ff4d4f' }}
              prefix={<ExclamationCircleOutlined />}
            />
          </Col>
          <Col span={3}>
            <Statistic
              title="停止"
              value={stats.status_counts.stopped || 0}
              valueStyle={{ color: '#d9d9d9' }}
              prefix={<StopOutlined />}
            />
          </Col>
          <Col span={9}>
            <Card type="inner" size="small" title="状态分布">
              <Progress
                percent={100}
                success={{ percent: ((stats.status_counts.running || 0) / (stats.total_streams || 1)) * 100 }}
                strokeColor={{
                  '0%': '#108ee9',
                  '100%': '#87d068',
                }}
                showInfo={false}
              />
              <div style={{ display: 'flex', justifyContent: 'space-between', fontSize: 12, marginTop: 4 }}>
                <span style={{ color: '#52c41a' }}>Running: {stats.status_counts.running || 0}</span>
                <span style={{ color: '#1890ff' }}>Starting: {stats.status_counts.starting || 0}</span>
                <span style={{ color: '#ff4d4f' }}>Error: {stats.status_counts.error || 0}</span>
              </div>
            </Card>
          </Col>
        </Row>
      </div>

      {/* 最近状态转换 */}
      <div>
        <div style={{ marginBottom: 16, fontWeight: 'bold', fontSize: 16 }}>最近状态转换</div>
        <Tabs defaultActiveKey="timeline" items={tabItems} type="card" />
      </div>
    </Card>
  );
}
