import React, { useMemo } from 'react';
import { Col, Card, Button, Tooltip, Tag } from 'antd';
import { FullscreenOutlined, PlayCircleOutlined } from '@ant-design/icons';
import StreamSnapshot from './StreamSnapshot';
import { Stream } from '@/types/stream';

interface VideoGridItemProps {
    stream: Stream;
    config: { span: number };
    deviceNameMap: Map<string, string>;
    onFullscreen: (streamKey: string) => void;
}

const VideoGridItem: React.FC<VideoGridItemProps> = ({ stream, config, deviceNameMap, onFullscreen }) => {
    const streamKey = `${stream.app}/${stream.stream}`;

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

    // 计算流是否可用
    const isStreamAvailable = useMemo(() => {
        return (stream.status === 'running' || stream.status === 2 || stream.status === 'starting' || stream.status === 1) &&
            ((stream.gateway_type === 'httpflv_gateway' && (stream.status === 'running' || stream.status === 2)) ||
                (stream.gateway_type === 'gb28181_gateway' && (stream.status === 'running' || stream.status === 2) &&
                    (stream.zlm_alive === true || stream.zlm_alive === undefined ||
                        (stream.bytes_speed && stream.bytes_speed > 0) ||
                        (stream.total_bytes && stream.total_bytes > 0))) ||
                stream.zlm_alive === true || stream.zlm_alive === undefined || stream.status === 1 || stream.status === 'starting');
    }, [stream.status, stream.gateway_type, stream.zlm_alive, stream.bytes_speed, stream.total_bytes]);

    return (
        <Col key={streamKey} xs={24} sm={12} md={config.span} lg={config.span}>
            <Card
                title={
                    <div style={{ display: 'flex', flexDirection: 'column', gap: 4 }}>
                        <div style={{ display: 'flex', justifyContent: 'space-between', alignItems: 'center' }}>
                            <div style={{ display: 'flex', alignItems: 'center', gap: 8, flexWrap: 'wrap' }}>
                                <span>{streamKey}</span>
                                {stream.device_id && stream.protocol === 'local-camera' && (
                                    <span style={{ color: '#8c8c8c', fontSize: '12px' }}>
                                        {deviceNameMap.get(stream.device_id) || stream.device_id}
                                    </span>
                                )}
                            </div>
                            <Tooltip title="全屏播放">
                                <Button
                                    type="text"
                                    size="small"
                                    icon={<FullscreenOutlined />}
                                    onClick={() => onFullscreen(streamKey)}
                                />
                            </Tooltip>
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
                }
                style={{ height: '100%' }}
                styles={{ body: { padding: 8 } }}
            >
                <div
                    style={{
                        aspectRatio: '16/9',
                        position: 'relative',
                        cursor: 'pointer',
                        backgroundColor: '#000',
                        overflow: 'hidden'
                    }}
                    onClick={() => onFullscreen(streamKey)}
                >
                    {/* 显示快照 */}
                    <StreamSnapshot
                        app={stream.app}
                        stream={stream.stream}
                        streamKey={streamKey}
                        isStreamAvailable={isStreamAvailable}
                        status={stream.status}
                        outputProtocol={stream.output_protocol}
                        zlmAlive={stream.zlm_alive}
                        uptimeSeconds={stream.uptime_seconds}
                    />
                    {/* 播放按钮覆盖层 */}
                    <div
                        style={{
                            position: 'absolute',
                            top: 0,
                            left: 0,
                            right: 0,
                            bottom: 0,
                            display: 'flex',
                            alignItems: 'center',
                            justifyContent: 'center',
                            backgroundColor: 'rgba(0, 0, 0, 0.3)',
                            opacity: 0,
                            transition: 'opacity 0.3s ease',
                            pointerEvents: 'none',
                        }}
                        onMouseEnter={(e) => {
                            e.currentTarget.style.opacity = '1';
                        }}
                        onMouseLeave={(e) => {
                            e.currentTarget.style.opacity = '0';
                        }}
                    >
                        <PlayCircleOutlined
                            style={{
                                fontSize: '48px',
                                color: 'white',
                            }}
                        />
                    </div>
                </div>
            </Card>
        </Col>
    );
};

export default VideoGridItem;
