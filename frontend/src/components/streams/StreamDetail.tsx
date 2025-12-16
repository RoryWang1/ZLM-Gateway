import { Modal, Descriptions, Tag, Typography } from 'antd';
import type { Stream } from '@/types/stream';
import ProtocolBadge from '@/components/common/ProtocolBadge/ProtocolBadge';
import { STATUS_COLORS } from '@/utils/constants';

const { Text } = Typography;

interface StreamDetailProps {
  open: boolean;
  stream: Stream | null;
  onCancel: () => void;
}

export default function StreamDetail({ open, stream, onCancel }: StreamDetailProps) {
  if (!stream) return null;

  return (
    <Modal title="流详情" open={open} onCancel={onCancel} footer={null} width={600}>
      <Descriptions column={1} bordered>
        <Descriptions.Item label="应用/流">
          <Text strong>{stream.app}/{stream.stream}</Text>
        </Descriptions.Item>
        <Descriptions.Item label="协议">
          <ProtocolBadge protocol={stream.protocol} gatewayType={stream.gateway_type} />
        </Descriptions.Item>
        <Descriptions.Item label="状态">
          <div>
            {(() => {
              const statusText = typeof stream.status === 'number'
                ? (stream.status === 2 ? 'running' : stream.status_text || 'unknown')
                : (stream.status || stream.status_text || 'unknown');
              return (
                <>
                  <Tag color={STATUS_COLORS[statusText as keyof typeof STATUS_COLORS] || 'default'}>
                    {statusText}
                  </Tag>
                  {statusText === 'error' && stream.error_message && (
                    <div style={{ marginTop: 8 }}>
                      <Tag color="red" style={{ marginRight: 8 }}>
                        错误码: {stream.error_code || 'UNKNOWN'}
                      </Tag>
                      <div style={{ color: '#ff4d4f', fontSize: 12, marginTop: 4 }}>
                        {stream.error_message}
                      </div>
                    </div>
                  )}
                </>
              );
            })()}
          </div>
        </Descriptions.Item>
        <Descriptions.Item label="源地址">
          <Text copyable>{stream.source_url}</Text>
        </Descriptions.Item>
        <Descriptions.Item label="进程ID">{stream.pid || '-'}</Descriptions.Item>
        <Descriptions.Item label="CPU使用率">
          {stream.cpu_usage ? `${stream.cpu_usage.toFixed(2)}%` : '-'}
        </Descriptions.Item>
        <Descriptions.Item label="内存使用">
          {stream.memory_usage
            ? `${(stream.memory_usage / 1024 / 1024).toFixed(2)} MB`
            : '-'}
        </Descriptions.Item>
        <Descriptions.Item label="观看者数">{stream.reader_count || 0}</Descriptions.Item>
        <Descriptions.Item label="传输速度">
          {stream.bytes_speed
            ? `${(stream.bytes_speed / 1024 / 1024).toFixed(2)} MB/s`
            : '-'}
        </Descriptions.Item>
        <Descriptions.Item label="总流量">
          {stream.total_bytes
            ? `${(stream.total_bytes / 1024 / 1024).toFixed(2)} MB`
            : '-'}
        </Descriptions.Item>
        <Descriptions.Item label="运行时间">
          {stream.uptime_seconds
            ? `${Math.floor(stream.uptime_seconds / 3600)}h ${Math.floor(
                (stream.uptime_seconds % 3600) / 60
              )}m`
            : '-'}
        </Descriptions.Item>
      </Descriptions>
    </Modal>
  );
}

