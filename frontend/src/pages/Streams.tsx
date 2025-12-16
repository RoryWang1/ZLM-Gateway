import { useState } from 'react';
import { Typography, Button, Space, Input } from 'antd';
import { PlusOutlined, SearchOutlined } from '@ant-design/icons';
import { useStreams, useStartStream, useStopStream, useDeleteStream, useStreamUpdates } from '@/hooks/useStreams';
import StreamTable from '@/components/streams/StreamTable';
import StreamForm from '@/components/streams/StreamForm';
import StreamDetail from '@/components/streams/StreamDetail';
import Loading from '@/components/common/Loading/Loading';
import Empty from '@/components/common/Empty/Empty';
import type { Stream, StartStreamRequest } from '@/types/stream';

const { Title } = Typography;

export default function Streams() {
  const [formOpen, setFormOpen] = useState(false);
  const [detailOpen, setDetailOpen] = useState(false);
  const [selectedStream, setSelectedStream] = useState<Stream | null>(null);
  const [searchText, setSearchText] = useState('');

  // 启用 WebSocket 实时更新
  useStreamUpdates();

  const { data: streams = [], isLoading } = useStreams();
  const startStreamMutation = useStartStream();
  const stopStreamMutation = useStopStream();
  const deleteStreamMutation = useDeleteStream();

  // 过滤流
  const filteredStreams = streams.filter((stream) => {
    if (!searchText) return true;
    const searchLower = searchText.toLowerCase();
    return (
      stream.app.toLowerCase().includes(searchLower) ||
      stream.stream.toLowerCase().includes(searchLower) ||
      stream.source_url.toLowerCase().includes(searchLower) ||
      stream.protocol.toLowerCase().includes(searchLower)
    );
  });

  const handleStart = async (values: StartStreamRequest) => {
    try {
      // 发送启动请求，但不等待流完全启动
      // 一旦收到API响应（确认请求已接收），就关闭窗口
      await startStreamMutation.mutateAsync(values);
      // API响应成功，立即关闭窗口
      setFormOpen(false);
    } catch (error) {
      // 即使出错也关闭窗口，错误信息已通过message显示
      setFormOpen(false);
    }
  };

  const handleStop = async (stream: Stream) => {
    await stopStreamMutation.mutateAsync({
      protocol: stream.protocol,
      app: stream.app,
      stream: stream.stream,
    });
  };

  const handleView = (stream: Stream) => {
    setSelectedStream(stream);
    setDetailOpen(true);
  };

  const handleDelete = async (stream: Stream) => {
    await deleteStreamMutation.mutateAsync({ app: stream.app, stream: stream.stream });
  };

  return (
    <div>
      <div style={{ display: 'flex', justifyContent: 'space-between', marginBottom: 16 }}>
        <Title level={2} style={{ margin: 0 }}>
          流管理
        </Title>
        <Space>
          <Input
            placeholder="搜索流..."
            allowClear
            style={{ width: 300 }}
            value={searchText}
            onChange={(e) => setSearchText(e.target.value)}
            prefix={<SearchOutlined />}
          />
          <Button type="primary" icon={<PlusOutlined />} onClick={() => setFormOpen(true)}>
            启动流
          </Button>
        </Space>
      </div>

      {isLoading ? (
        <Loading />
      ) : filteredStreams.length === 0 ? (
        <Empty description={searchText ? '没有找到匹配的流' : '暂无流'} />
      ) : (
        <StreamTable
          streams={filteredStreams}
          loading={isLoading}
          onStart={(stream) => {
            handleStart({
              protocol: stream.protocol,
              output_protocol: stream.output_protocol || 'http-flv',  // 使用流的 output_protocol，如果没有则使用默认值
              source_url: stream.source_url,
              app: stream.app,
              stream: stream.stream,
            });
          }}
          onStop={handleStop}
          onView={handleView}
          onDelete={handleDelete}
        />
      )}

      <StreamForm
        open={formOpen}
        onCancel={() => {
          setFormOpen(false);
        }}
        onOk={handleStart}
      />

      <StreamDetail
        open={detailOpen}
        stream={selectedStream}
        onCancel={() => {
          setDetailOpen(false);
          setSelectedStream(null);
        }}
      />
    </div>
  );
}
