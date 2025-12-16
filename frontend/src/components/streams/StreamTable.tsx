import { Table, Button, Tag, Space, Popconfirm, Tooltip } from 'antd';
import { PlayCircleOutlined, StopOutlined, EyeOutlined, DeleteOutlined } from '@ant-design/icons';
import type { Stream } from '@/types/stream';
import ProtocolBadge from '@/components/common/ProtocolBadge/ProtocolBadge';
import { STATUS_COLORS } from '@/utils/constants';

interface StreamTableProps {
  streams: Stream[];
  loading?: boolean;
  onStart?: (stream: Stream) => void;
  onStop?: (stream: Stream) => void;
  onView?: (stream: Stream) => void;
  onDelete?: (stream: Stream) => void;
}

export default function StreamTable({
  streams,
  loading = false,
  onStart,
  onStop,
  onView,
  onDelete,
}: StreamTableProps) {
  const columns = [
    {
      title: '应用/流',
      key: 'stream',
      render: (_: any, record: Stream) => (
        <div>
          <div style={{ fontWeight: 500 }}>{record.app}/{record.stream}</div>
          <div style={{ fontSize: 12, color: '#8c8c8c' }}>{record.source_url}</div>
        </div>
      ),
    },
    {
      title: '输入协议',
      key: 'protocol',
      render: (_: any, record: Stream) => (
        <ProtocolBadge
          protocol={record.protocol}
          gatewayType={record.gateway_type}
          processingType={record.processing_type}
        />
      ),
    },
    {
      title: '输出协议',
      key: 'output_protocol',
      render: (_: any, record: Stream) => {
        const outputProtocol = record.output_protocol;
        // 如果 output_protocol 为空或无效，显示默认值而不是 AUTO
        const displayProtocol = (outputProtocol && ['http-flv', 'hls', 'webrtc'].includes(outputProtocol))
          ? outputProtocol.toUpperCase()
          : 'HTTP-FLV (默认)';
        return (
          <Tag color={outputProtocol === 'webrtc' ? 'purple' : 'blue'}>
            {displayProtocol}
          </Tag>
        );
      },
    },
    {
      title: '状态',
      dataIndex: 'status',
      key: 'status',
      render: (status: number | string, record: Stream) => {
        // 处理数字状态码（2 = running）或字符串状态
        const statusText = typeof status === 'number'
          ? (status === 2 ? 'running' : record.status_text || 'unknown')
          : (status || record.status_text || 'unknown');

        // 如果有错误信息，显示错误提示
        const hasError = statusText === 'error' && record.error_message;

        return (
          <div>
            <Tag color={STATUS_COLORS[statusText as keyof typeof STATUS_COLORS] || 'default'}>
              {statusText}
            </Tag>
            {hasError && (
              <Tooltip title={record.error_message}>
                <Tag color="red" style={{ marginLeft: 4, cursor: 'help' }}>
                  {record.error_code || 'ERROR'}
                </Tag>
              </Tooltip>
            )}
          </div>
        );
      },
    },
    {
      title: '观看者',
      dataIndex: 'reader_count',
      key: 'reader_count',
      render: (count: number) => count || 0,
    },
    // 隐藏 CPU 和内存列
    // {
    //   title: 'CPU',
    //   dataIndex: 'cpu_usage',
    //   key: 'cpu_usage',
    //   render: (usage: number) => (usage ? `${usage.toFixed(1)}%` : '-'),
    // },
    // {
    //   title: '内存',
    //   dataIndex: 'memory_usage',
    //   key: 'memory_usage',
    //   render: (usage: number) => {
    //     if (!usage) return '-';
    //     const mb = (usage / 1024 / 1024).toFixed(1);
    //     return `${mb} MB`;
    //   },
    // },
    {
      title: '传输速度',
      dataIndex: 'bytes_speed',
      key: 'bytes_speed',
      render: (speed: number) => {
        if (!speed || speed === 0) return '-';
        // bytes_speed 是字节/秒，转换为 MB/s
        const mbps = (speed / 1024 / 1024).toFixed(2);
        return `${mbps} MB/s`;
      },
    },
    {
      title: '操作',
      key: 'action',
      render: (_: any, record: Stream) => (
        <Space>
          {record.status === 'running' ? (
            <Popconfirm
              title="确定要停止这个流吗？"
              onConfirm={() => onStop?.(record)}
              okText="确定"
              cancelText="取消"
            >
              <Button type="primary" danger size="small" icon={<StopOutlined />}>
                停止
              </Button>
            </Popconfirm>
          ) : (
            <Button
              type="primary"
              size="small"
              icon={<PlayCircleOutlined />}
              disabled={!record.source_url}
              title={!record.source_url ? '源地址未知，无法启动' : '启动流'}
              onClick={() => onStart?.(record)}
            >
              启动
            </Button>
          )}
          <Button size="small" icon={<EyeOutlined />} onClick={() => onView?.(record)}>
            详情
          </Button>
          <Popconfirm
            title="确定要删除这个流吗？"
            description="删除后无法恢复"
            onConfirm={() => onDelete?.(record)}
            okText="确定"
            cancelText="取消"
          >
            <Button size="small" danger icon={<DeleteOutlined />}>
              删除
            </Button>
          </Popconfirm>
        </Space>
      ),
    },
  ];

  return (
    <Table
      columns={columns}
      dataSource={streams}
      loading={loading}
      rowKey={(record) => `${record.app}/${record.stream}`}
      pagination={{
        pageSize: 10,
        showSizeChanger: true,
        showTotal: (total) => `共 ${total} 条`,
      }}
    />
  );
}

