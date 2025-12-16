import { useState } from 'react';
import { Typography, Space, Checkbox } from 'antd';
import { useStreams, useStreamUpdates } from '@/hooks/useStreams';
import VideoGrid from '@/components/video/VideoGrid';
import Loading from '@/components/common/Loading/Loading';
import Empty from '@/components/common/Empty/Empty';

const { Title } = Typography;

export default function VideoView() {
  // 启用 WebSocket 实时更新
  useStreamUpdates();
  
  const { data: streams = [], isLoading } = useStreams();
  const [showOnlyRunning, setShowOnlyRunning] = useState(true);

  // 过滤流：只显示运行中的流（包括 starting 状态，因为流正在启动）
  // 如果取消勾选，也显示 Error 状态的流（让用户看到问题）
  const filteredStreams = showOnlyRunning
    ? streams.filter((stream) => {
      // Backend may return numeric status (e.g., 2 = running, 1 = starting, 4 = error)
      if (typeof stream.status === 'number') {
        // 显示 running (2) 和 starting (1) 状态的流
        return stream.status === 2 || stream.status === 1;
      }
      // Fallback to string comparisons
      return stream.status === 'running' || stream.status === 'starting' || 
             stream.status_text === 'running' || stream.status_text === 'starting';
    })
    : streams; // 显示所有流，包括 Error 状态



  return (
    <div>
      <div style={{ display: 'flex', justifyContent: 'space-between', marginBottom: 16 }}>
        <Title level={2} style={{ margin: 0 }}>
          视频展示
        </Title>
        <Space>
          <Checkbox
            checked={showOnlyRunning}
            onChange={(e) => setShowOnlyRunning(e.target.checked)}
          >
            只显示运行中的流
          </Checkbox>
        </Space>
      </div>

      {/* Only show Loading on initial load, not on background refetch */}
      {isLoading && streams.length === 0 ? (
        <Loading />
      ) : filteredStreams.length === 0 ? (
        <Empty description={showOnlyRunning ? '暂无运行中的流' : '暂无流'} />
      ) : (
        <VideoGrid streams={filteredStreams} />
      )}
    </div>
  );
}
