// 统一视频播放器组件

import React from 'react';
import { Alert } from 'antd';
import FLVPlayer from './FLVPlayer';
import HLSPlayer from './HLSPlayer';
import WebRTCPlayer from './WebRTCPlayer';
import { getStreamPlayURL } from '@/utils/streamUrl';
import type { PlayProtocol } from '@/utils/streamUrl';

interface VideoPlayerProps {
  app: string;
  stream: string;
  isStreamAvailable?: boolean;
  defaultProtocol?: PlayProtocol;
  autoPlay?: boolean;
  style?: React.CSSProperties;
  status?: number | string; // 流状态：1=starting, 2=running, 3=stopped, 4=error
}

const VideoPlayer = React.memo(({
  app,
  stream,
  isStreamAvailable = true,
  // 默认使用 HTTP-FLV；具体流会根据 output_protocol 覆盖为 webrtc/http-flv/hls
  defaultProtocol = 'http-flv',
  autoPlay = true,
  style,
  status,
}: VideoPlayerProps) => {
  // 直接使用用户选择的播放协议（从VideoGrid的协议下拉框传入）
  // 所有流都使用同一个播放协议，与输入协议无关
  // 验证 protocol 是否有效，如果无效则使用默认值
  const validProtocols: PlayProtocol[] = ['http-flv', 'hls', 'webrtc'];
  const protocol: PlayProtocol = validProtocols.includes(defaultProtocol as PlayProtocol) 
    ? defaultProtocol as PlayProtocol 
    : 'http-flv';
  const url = getStreamPlayURL(app, stream, protocol);

  // 记录协议切换日志


  const renderPlayer = () => {
    // 如果流不可用，显示错误提示
    if (!isStreamAvailable) {
      // 检查是否是 Error 状态（status === 4 或 status === 'error'）
      const isErrorStatus = status !== undefined && (status === 4 || status === 'error');
      
      return (
        <Alert
          message={isErrorStatus ? "流启动失败" : "流不可用"}
          description={isErrorStatus 
            ? "流启动失败，可能是源流不存在或无法连接。请检查源流地址或稍后重试。"
            : "流未成功推送到 ZLMediaKit，无法播放。请检查流状态或稍后重试。"
          }
          type={isErrorStatus ? "error" : "warning"}
          showIcon
          style={{ width: '100%', height: '100%' }}
        />
      );
    }

    // 播放器样式：充满剩余空间
    const playerStyle: React.CSSProperties = { width: '100%', height: '100%' };

    // 使用 key 确保协议切换时强制重新渲染播放器组件
    // key 中包含 protocol，所以当 protocol 变化时，React 会自动卸载旧组件并挂载新组件
    switch (protocol) {
      case 'http-flv':
        return <FLVPlayer key={`flv-${app}-${stream}`} url={url} autoPlay={autoPlay} style={playerStyle} />;
      case 'hls':
        return <HLSPlayer key={`hls-${app}-${stream}`} url={url} autoPlay={autoPlay} style={playerStyle} />;
      case 'webrtc':
        return <WebRTCPlayer key={`webrtc-${app}-${stream}`} url={url} autoPlay={autoPlay} className="w-full h-full" />;
      default:
        // 如果 protocol 无效，显示错误提示
        return (
          <Alert
            message="播放协议错误"
            description={`不支持的播放协议: ${protocol}。请检查流的输出协议配置。`}
            type="error"
            showIcon
            style={{ width: '100%', height: '100%' }}
          />
        );
    }
  };

  return (
    <div style={{ ...style, display: 'flex', flexDirection: 'column' }}>
      <div style={{ flex: 1, position: 'relative', overflow: 'hidden' }}>
        {renderPlayer()}
      </div>
    </div>
  );
});

export default VideoPlayer;

