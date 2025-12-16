// 设备卡片组件

import { Card, Button, Space, Tag, Descriptions, Modal, message, Select } from 'antd';
import { PlayCircleOutlined, ReloadOutlined, EyeOutlined, StopOutlined } from '@ant-design/icons';
import { useState, useMemo } from 'react';
import { useQueryClient } from '@tanstack/react-query';
import type { Device } from '@/types/device';
import { useDeviceStreams, useStartDeviceStream } from '@/hooks/useDevices';
import { useStartStream, useStreams, useStopStream } from '@/hooks/useStreams';
import { stopLocalCameraStream, stopGB28181DeviceStream } from '@/api/devices';

type OutputProtocol = 'http-flv' | 'hls' | 'webrtc';

interface DeviceCardProps {
  device: Device;
  onRefresh?: () => void;
}

export default function DeviceCard({ device, onRefresh }: DeviceCardProps) {
  const [streamsModalOpen, setStreamsModalOpen] = useState(false);
  const [detailModalOpen, setDetailModalOpen] = useState(false);
  const [outputProtocol, setOutputProtocol] = useState<OutputProtocol>('http-flv');
  const queryClient = useQueryClient();

  const { data: streams = [], isLoading: streamsLoading } = useDeviceStreams(
    device.protocol,
    device.device_id
  );
  const { data: allStreams = [] } = useStreams();
  const startDeviceStream = useStartDeviceStream(device.protocol);
  const startStream = useStartStream();
  const stopStream = useStopStream();
  
  // 查找该设备的所有流（通过device_id匹配）
  // 对于本地摄像头，也通过 source_url 匹配（格式：local-camera://{index} 或 local-camera://device_id:{device_id}）
  const deviceStreams = useMemo(() => {
    if (device.protocol === 'local-camera') {
      // 优先通过 device_id 匹配
      const deviceIdMatch = allStreams.filter(s => s.device_id === device.device_id);
      if (deviceIdMatch.length > 0) {
        return deviceIdMatch;
      }
      
      // 如果通过 device_id 没找到，尝试通过 source_url 匹配
      if (device.index !== undefined) {
        const sourceUrlMatch = allStreams.filter(s => {
          if (!s.source_url) return false;
          // 匹配 local-camera://{index} 格式
          if (s.source_url === `local-camera://${device.index}`) {
            return true;
          }
          // 匹配 local-camera://device_id:{device_id} 格式
          if (s.source_url === `local-camera://device_id:${device.device_id}`) {
            return true;
          }
          // 匹配 local-camera://device_id:{device_id} 格式（带其他参数）
          if (s.source_url.startsWith(`local-camera://device_id:${device.device_id}`)) {
            return true;
          }
          return false;
        });
        return sourceUrlMatch;
      }
      return [];
    }
    return [];
  }, [allStreams, device.device_id, device.protocol, device.index]);
  
  // 检查是否有运行中的流
  const hasRunningStream = useMemo(() => {
    return deviceStreams.some(s => 
      s.status === 2 || s.status === 'running' || 
      s.status_text === 'running' || s.status === 1 || s.status === 'starting'
    );
  }, [deviceStreams]);

  const handleGetStreams = () => {
    setStreamsModalOpen(true);
  };

  const handleStartStream = async (streamUrl?: string) => {
    try {
      // 如果设备协议是 ONVIF/ISAPI 等，使用设备流启动
      if (device.protocol === 'onvif' || device.protocol === 'isapi' || device.protocol === 'dahua' || device.protocol === 'psia') {
        await startDeviceStream.mutateAsync({
          deviceId: device.device_id,
          app: 'live',
          stream: `device_${device.device_id}_${Date.now()}`,
        });
      } else if (device.protocol === 'gb28181') {
        // GB28181设备启动流（使用设备ID作为通道ID）
        await startDeviceStream.mutateAsync({
          deviceId: device.device_id,
          channelId: device.device_id,  // GB28181通常使用设备ID作为通道ID
          app: 'live',
          stream: `gb28181_${device.device_id}_${Date.now()}`,
        });
      } else if (device.protocol === 'local-camera') {
        // 本地摄像头直接启动流
        // 使用 device_id 生成流名，确保每个设备有唯一的流名（即使索引变化）
        // 流名格式: camera_{device_id的hash或后缀}，例如 camera_6a85fea4600a581
        const deviceIdSuffix = device.device_id.replace('local-camera-', '');
        const streamName = `camera_${deviceIdSuffix}`;
        await startDeviceStream.mutateAsync({
          deviceId: device.device_id,
          app: 'live',
          stream: streamName,
          // 使用用户选择的输出协议
          outputProtocol: outputProtocol,
        });
      } else {
        // 否则直接启动流
        if (!streamUrl) {
          message.error('流地址不能为空');
          return;
        }
        await startStream.mutateAsync({
          protocol: 'rtsp',
          output_protocol: 'http-flv',  // 默认使用 HTTP-FLV
          source_url: streamUrl,
          app: 'live',
          stream: `device_${device.device_id}_${Date.now()}`,
        });
      }
      message.success('流启动成功');
      setStreamsModalOpen(false);
      // 立即刷新流列表，确保按钮状态正确更新
      queryClient.invalidateQueries({ queryKey: ['streams'] });
      // 等待一小段时间后再次刷新，确保后端状态已更新
      setTimeout(() => {
        queryClient.invalidateQueries({ queryKey: ['streams'] });
      }, 1000);
      // 刷新设备列表
      if (onRefresh) {
        onRefresh();
      }
    } catch (error: any) {
      // 检查是否是设备被占用的错误
      if (error.response?.status === 409 || error.message?.includes('already in use') || error.message?.includes('已被占用')) {
        // 提取流信息（如果有）
        const streamMatch = error.message?.match(/stream:\s*([^\s.]+)/);
        const streamInfo = streamMatch ? ` (${streamMatch[1]})` : '';
        message.warning({
          content: `设备正在被其他流使用${streamInfo}，请先停止现有流`,
          duration: 5,
        });
        // 刷新流列表，显示当前运行的流
        queryClient.invalidateQueries({ queryKey: ['streams'] });
      } else {
        message.error(`流启动失败: ${error.message}`);
      }
    }
  };

  return (
    <>
      <Card
        title={
          <div style={{ display: 'flex', justifyContent: 'space-between', alignItems: 'center' }}>
            <span>{device.name || device.device_id}</span>
            <Tag color="blue">{device.protocol?.toUpperCase() || 'UNKNOWN'}</Tag>
          </div>
        }
        extra={
          <Space>
            <Button size="small" icon={<ReloadOutlined />} onClick={onRefresh}>
              刷新
            </Button>
            <Button size="small" icon={<EyeOutlined />} onClick={() => setDetailModalOpen(true)}>
              详情
            </Button>
          </Space>
        }
        style={{ marginBottom: 16 }}
      >
        <Descriptions column={1} size="small">
          {device.protocol === 'local-camera' ? (
            <>
              <Descriptions.Item label="平台">{device.platform || '未知'}</Descriptions.Item>
              <Descriptions.Item label="设备索引">{device.index ?? '-'}</Descriptions.Item>
              {device.device_path && (
                <Descriptions.Item label="设备路径">{device.device_path}</Descriptions.Item>
              )}
            </>
          ) : device.protocol === 'gb28181' ? (
            <>
              <Descriptions.Item label="设备ID">{device.device_id || '-'}</Descriptions.Item>
              <Descriptions.Item label="IP地址">{device.ip || '-'}</Descriptions.Item>
              <Descriptions.Item label="端口">{device.port || '-'}</Descriptions.Item>
              {device.manufacturer && (
                <Descriptions.Item label="制造商">{device.manufacturer}</Descriptions.Item>
              )}
              {device.model && (
                <Descriptions.Item label="型号">{device.model}</Descriptions.Item>
              )}
            </>
          ) : (
            <>
              <Descriptions.Item label="IP地址">{device.ip || '-'}</Descriptions.Item>
              <Descriptions.Item label="端口">{device.port || '-'}</Descriptions.Item>
              {device.manufacturer && (
                <Descriptions.Item label="厂商">{device.manufacturer}</Descriptions.Item>
              )}
              {device.model && <Descriptions.Item label="型号">{device.model}</Descriptions.Item>}
            </>
          )}
        </Descriptions>
        <div style={{ marginTop: 16 }}>
          {device.protocol === 'local-camera' ? (
            <>
              {/* 本地摄像头：始终显示启动/停止按钮 */}
              {hasRunningStream ? (
                <Space direction="vertical" style={{ width: '100%' }}>
                  <Button
                    type="primary"
                    danger
                    icon={<StopOutlined />}
                    onClick={async () => {
                      // 停止该设备的所有流
                      for (const stream of deviceStreams) {
                        if (stream.status === 2 || stream.status === 'running' || stream.status_text === 'running') {
                          try {
                            await stopLocalCameraStream(stream.app, stream.stream);
                            message.success(`流 ${stream.app}/${stream.stream} 已停止`);
                          } catch (error: any) {
                            message.error(`停止流失败: ${error.message}`);
                          }
                        }
                      }
                      // 刷新流列表，确保按钮状态更新
                      queryClient.invalidateQueries({ queryKey: ['streams'] });
                      if (onRefresh) {
                        onRefresh();
                      }
                    }}
                    block
                  >
                    停止摄像头流
                  </Button>
                  <div style={{ fontSize: '12px', color: '#8c8c8c', textAlign: 'center' }}>
                    已启动 {deviceStreams.filter(s => s.status === 2 || s.status === 'running' || s.status_text === 'running').length} 个流
                  </div>
                </Space>
              ) : (
                <Space direction="vertical" style={{ width: '100%' }}>
                  <Select
                    value={outputProtocol}
                    onChange={(value) => setOutputProtocol(value)}
                    style={{ width: '100%' }}
                    options={[
                      { label: 'HTTP-FLV', value: 'http-flv' },
                      { label: 'HLS', value: 'hls' },
                      { label: 'WebRTC', value: 'webrtc' },
                    ]}
                    placeholder="选择输出协议"
                  />
                <Button
                  type="primary"
                  icon={<PlayCircleOutlined />}
                  onClick={() => handleStartStream()}
                  block
                  loading={startDeviceStream.isPending}
                >
                  启动摄像头流
                </Button>
                </Space>
              )}
            </>
          ) : device.protocol === 'gb28181' ? (
            <>
              {/* GB28181设备：直接显示启动/停止按钮 */}
              {hasRunningStream ? (
                <Space direction="vertical" style={{ width: '100%' }}>
                  <Button
                    type="primary"
                    danger
                    icon={<StopOutlined />}
                    onClick={async () => {
                      // 停止该设备的所有流
                      // GB28181设备需要使用专门的停止API
                      if (device.protocol === 'gb28181') {
                        // 从stream名称中提取device_id和channel_id
                        // stream格式：device_id_channel_id 或 device_id_channel_id_timestamp
                        for (const stream of deviceStreams) {
                          if (stream.status === 2 || stream.status === 'running' || stream.status_text === 'running') {
                            try {
                              // 从stream名称中提取device_id和channel_id
                              // stream格式：device_id_channel_id 或 device_id_channel_id_timestamp
                              const streamName = stream.stream;
                              // 去掉时间戳（如果有）
                              const baseStreamName = streamName.includes('_') 
                                ? streamName.split('_').slice(0, 2).join('_')
                                : streamName;
                              const parts = baseStreamName.split('_');
                              if (parts.length >= 2) {
                                const deviceId = parts[0];
                                const channelId = parts[1];
                                await stopGB28181DeviceStream(deviceId, channelId);
                                message.success(`流 ${stream.app}/${stream.stream} 已停止`);
                              } else {
                                message.error(`无法解析流名称: ${streamName}`);
                              }
                            } catch (error: any) {
                              message.error(`停止流失败: ${error.message}`);
                            }
                          }
                        }
                      } else {
                        // 其他协议使用通用停止API
                        for (const stream of deviceStreams) {
                          if (stream.status === 2 || stream.status === 'running' || stream.status_text === 'running') {
                            try {
                              await stopStream.mutateAsync({
                                protocol: stream.protocol as any,
                                app: stream.app,
                                stream: stream.stream,
                              });
                              message.success(`流 ${stream.app}/${stream.stream} 已停止`);
                            } catch (error: any) {
                              message.error(`停止流失败: ${error.message}`);
                            }
                          }
                        }
                      }
                      // 刷新流列表，确保按钮状态更新
                      queryClient.invalidateQueries({ queryKey: ['streams'] });
                      if (onRefresh) {
                        onRefresh();
                      }
                    }}
                    block
                  >
                    停止流
                  </Button>
                  <div style={{ fontSize: '12px', color: '#8c8c8c', textAlign: 'center' }}>
                    已启动 {deviceStreams.filter(s => s.status === 2 || s.status === 'running' || s.status_text === 'running').length} 个流
                  </div>
                </Space>
              ) : (
                <Space direction="vertical" style={{ width: '100%' }}>
                  {!device.online && (
                    <div style={{ fontSize: '12px', color: '#ff4d4f', textAlign: 'center', marginBottom: 8 }}>
                      ⚠️ 设备离线，需要先通过SIP注册
                    </div>
                  )}
                  <Button
                    type="primary"
                    icon={<PlayCircleOutlined />}
                    onClick={() => handleStartStream()}
                    block
                    loading={startDeviceStream.isPending}
                    disabled={!device.online}
                  >
                    启动流
                  </Button>
                </Space>
              )}
            </>
          ) : (
            <Button
              type="primary"
              icon={<PlayCircleOutlined />}
              onClick={handleGetStreams}
              block
            >
              获取流地址
            </Button>
          )}
        </div>
      </Card>

      {/* 流地址列表弹窗 */}
      <Modal
        title="设备流地址"
        open={streamsModalOpen}
        onCancel={() => setStreamsModalOpen(false)}
        footer={null}
        width={600}
      >
        {streamsLoading ? (
          <div style={{ textAlign: 'center', padding: '40px' }}>加载中...</div>
        ) : streams.length === 0 ? (
          <div style={{ textAlign: 'center', padding: '40px', color: '#8c8c8c' }}>
            暂无流地址
          </div>
        ) : (
          <div>
            {streams.map((stream) => (
              <Card
                key={stream.stream_url}
                size="small"
                style={{ marginBottom: 8 }}
                extra={
                  <Button
                    type="primary"
                    size="small"
                    onClick={() => handleStartStream(stream.stream_url)}
                  >
                    启动流
                  </Button>
                }
              >
                <div>
                  <div style={{ fontWeight: 500, marginBottom: 4 }}>流地址:</div>
                  <div style={{ fontSize: 12, color: '#8c8c8c', wordBreak: 'break-all' }}>
                    {stream.stream_url}
                  </div>
                  {stream.profile && (
                    <div style={{ marginTop: 4, fontSize: 12 }}>
                      配置: {stream.profile}
                    </div>
                  )}
                  {stream.resolution && (
                    <div style={{ marginTop: 4, fontSize: 12 }}>
                      分辨率: {stream.resolution}
                    </div>
                  )}
                </div>
              </Card>
            ))}
          </div>
        )}
      </Modal>

      {/* 设备详情弹窗 */}
      <Modal
        title="设备详情"
        open={detailModalOpen}
        onCancel={() => setDetailModalOpen(false)}
        footer={null}
        width={600}
      >
        <Descriptions column={1} bordered>
          <Descriptions.Item label="设备ID">{device.device_id}</Descriptions.Item>
          <Descriptions.Item label="设备名称">{device.name || '-'}</Descriptions.Item>
          <Descriptions.Item label="协议">{device.protocol?.toUpperCase() || 'UNKNOWN'}</Descriptions.Item>
          {device.protocol === 'local-camera' ? (
            <>
              <Descriptions.Item label="平台">{device.platform || '未知'}</Descriptions.Item>
              <Descriptions.Item label="设备索引">{device.index ?? '-'}</Descriptions.Item>
              {device.device_path && (
                <Descriptions.Item label="设备路径">{device.device_path}</Descriptions.Item>
              )}
            </>
          ) : (
            <>
              <Descriptions.Item label="IP地址">{device.ip || '-'}</Descriptions.Item>
              <Descriptions.Item label="端口">{device.port || '-'}</Descriptions.Item>
              <Descriptions.Item label="厂商">
                {device.manufacturer || '未知'}
              </Descriptions.Item>
              <Descriptions.Item label="型号">{device.model || '未知'}</Descriptions.Item>
              <Descriptions.Item label="序列号">
                {device.serial_number || '未知'}
              </Descriptions.Item>
            </>
          )}
        </Descriptions>
      </Modal>
    </>
  );
}

