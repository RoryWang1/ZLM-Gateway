// 视频网格组件

import React, { useState, useMemo } from 'react';
import { Row, Button, Space, Select, Tag } from 'antd';
import { FullscreenExitOutlined } from '@ant-design/icons';
import VideoPlayer from './VideoPlayer';
import type { Stream } from '@/types/stream';
import type { PlayProtocol } from '@/utils/streamUrl';
import { useLocalCameras } from '@/hooks/useDevices';
import VideoGridItem from './VideoGridItem';

interface VideoGridProps {
  streams: Stream[];
  defaultProtocol?: PlayProtocol;
}

type GridLayout = '1x1' | '2x2' | '3x3' | '4x4';

const layoutConfigs: Record<GridLayout, { cols: number; span: number }> = {
  '1x1': { cols: 1, span: 24 },
  '2x2': { cols: 2, span: 12 },
  '3x3': { cols: 3, span: 8 },
  '4x4': { cols: 4, span: 6 },
};

export default function VideoGrid({ streams }: VideoGridProps) {
  const [layout, setLayout] = useState<GridLayout>('2x2');
  const [fullscreenStream, setFullscreenStream] = useState<string | null>(null);

  // 获取本地摄像头设备列表，用于显示设备名称
  const { data: localCameras = [] } = useLocalCameras();

  // 创建设备ID到设备名称的映射
  const deviceNameMap = useMemo(() => {
    const map = new Map<string, string>();
    localCameras.forEach(device => {
      map.set(device.device_id, device.name);
    });
    return map;
  }, [localCameras]);

  const config = layoutConfigs[layout];
  const displayStreams = streams.slice(0, config.cols * config.cols);

  // 获取协议显示名称
  const getProtocolLabel = (protocol?: string) => {
    if (!protocol) return '未知';
    const protocolMap: Record<string, string> = {
      'rtsp': 'RTSP',
      'rtmp': 'RTMP',
      'http-flv': 'HTTP-FLV',
      'hls': 'HLS',
      'dash': 'DASH',
      'quic': 'QUIC',
      'onvif': 'ONVIF',
      'isapi': 'ISAPI',
      'dahua': 'Dahua',
      'psia': 'PSIA',
      'webrtc': 'WebRTC',
      'local-camera': '本地摄像头',
    };
    return protocolMap[protocol] || protocol.toUpperCase();
  };

  const handleFullscreen = (streamKey: string) => {
    if (fullscreenStream === streamKey) {
      setFullscreenStream(null);
    } else {
      setFullscreenStream(streamKey);
    }
  };

  if (fullscreenStream) {
    const stream = streams.find((s) => `${s.app}/${s.stream}` === fullscreenStream);
    if (stream) {
      // 计算流是否可用
      // 对于 starting 状态的流，也允许尝试播放（流可能正在启动中）
      // 对于 running 状态的流，需要 zlm_alive 为 true 或 undefined
      // 对于 httpflv_gateway 类型的流，由于 ZLM 的 getMediaList 可能不返回 HTTP-FLV 代理流信息，
      // 所以只要 status 是 running，就认为流可用（不依赖 zlm_alive）
      // 对于 gb28181_gateway 类型的流，由于 ZLM 的 alive 字段可能有延迟，即使 zlm_alive=false，
      // 只要有数据传输（bytes_speed>0 或 total_bytes>0），也认为流可用
      const isStreamAvailable =
        (stream.status === 'running' || stream.status === 2 || stream.status === 'starting' || stream.status === 1) &&
        ((stream.gateway_type === 'httpflv_gateway' && (stream.status === 'running' || stream.status === 2)) ||
          (stream.gateway_type === 'gb28181_gateway' && (stream.status === 'running' || stream.status === 2) &&
            (stream.zlm_alive === true || stream.zlm_alive === undefined ||
              (stream.bytes_speed && stream.bytes_speed > 0) ||
              (stream.total_bytes && stream.total_bytes > 0))) ||
          stream.zlm_alive === true || stream.zlm_alive === undefined || stream.status === 1 || stream.status === 'starting');

      const streamKey = `${stream.app}/${stream.stream}`;
      const deviceName = stream.device_id && stream.protocol === 'local-camera'
        ? deviceNameMap.get(stream.device_id) || stream.device_id
        : null;

      return (
        <div style={{ position: 'relative', width: '100%', height: 'calc(100vh - 200px)' }}>
          <div style={{ position: 'absolute', top: 10, left: 10, right: 10, zIndex: 1000, display: 'flex', justifyContent: 'space-between', alignItems: 'flex-start', background: 'rgba(0, 0, 0, 0.7)', padding: '8px 12px', borderRadius: '4px' }}>
            <div style={{ display: 'flex', flexDirection: 'column', gap: 4, color: 'white' }}>
              <div style={{ display: 'flex', alignItems: 'center', gap: 8, flexWrap: 'wrap' }}>
                <span style={{ fontWeight: 'bold' }}>{streamKey}</span>
                {deviceName && (
                  <span style={{ fontSize: '12px', color: '#d9d9d9' }}>{deviceName}</span>
                )}
              </div>
              <div style={{ display: 'flex', gap: 4, flexWrap: 'wrap' }}>
                <Tag color="blue">输入: {getProtocolLabel(stream.protocol)}</Tag>
                {stream.output_protocol && (
                  <Tag color="green">输出: {
                    // GB28181只支持HLS和FLV，如果output_protocol是webrtc或其他不支持的协议，显示实际使用的协议
                    (stream.protocol === 'gb28181')
                      ? (stream.output_protocol === 'hls' || stream.output_protocol === 'http-flv')
                        ? getProtocolLabel(stream.output_protocol)  // 显示实际协议
                        : getProtocolLabel('http-flv')  // 默认显示FLV（因为GB28181不支持WebRTC）
                      : getProtocolLabel(stream.output_protocol)
                  }</Tag>
                )}
              </div>
            </div>
            <Button
              icon={<FullscreenExitOutlined />}
              onClick={() => setFullscreenStream(null)}
              type="primary"
            >
              退出全屏
            </Button>
          </div>
          <VideoPlayer
            app={stream.app}
            stream={stream.stream}
            isStreamAvailable={isStreamAvailable}
            defaultProtocol={
              // GB28181只支持HLS和FLV，不支持WebRTC
              // 如果output_protocol是hls，使用hls播放器
              // 如果output_protocol是http-flv，使用flv播放器
              // 如果output_protocol是webrtc或其他不支持的协议，默认使用http-flv
              (stream.protocol === 'gb28181')
                ? (stream.output_protocol === 'hls')
                  ? 'hls'  // 如果明确指定hls，使用hls播放器
                  : 'http-flv'  // 默认使用HTTP-FLV（GB28181不支持WebRTC）
                : (stream.output_protocol && ['http-flv', 'hls', 'webrtc'].includes(stream.output_protocol))
                  ? (stream.output_protocol as PlayProtocol)
                  : 'http-flv'  // 其他流默认使用HTTP-FLV
            }
            status={stream.status}
            style={{ width: '100%', height: '100%' }}
          />
        </div>
      );
    }
  }

  return (
    <div>
      <div style={{ marginBottom: 16, display: 'flex', justifyContent: 'space-between', alignItems: 'center' }}>
        <Space>
          <span>布局:</span>
          <Select
            value={layout}
            onChange={setLayout}
            style={{ width: 100 }}
            options={[
              { label: '1x1', value: '1x1' },
              { label: '2x2', value: '2x2' },
              { label: '3x3', value: '3x3' },
              { label: '4x4', value: '4x4' },
            ]}
          />
        </Space>
        <div>
          显示 {displayStreams.length} / {streams.length} 路视频
        </div>
      </div>

      <Row gutter={[16, 16]}>
        {displayStreams.map((stream) => (
          <VideoGridItem
            key={`${stream.app}/${stream.stream}`}
            stream={stream}
            config={config}
            deviceNameMap={deviceNameMap}
            onFullscreen={handleFullscreen}
          />
        ))}
      </Row>

      {displayStreams.length === 0 && (
        <div style={{ textAlign: 'center', padding: '40px', color: '#8c8c8c' }}>
          暂无视频流
        </div>
      )}
    </div>
  );
}
