// GB28181设备卡片组件 - 完整功能版本

import { Card, Button, Space, Tag, Descriptions, Modal, message, Select, Form, Input, Popconfirm } from 'antd';
import { 
  ReloadOutlined, 
  EyeOutlined, 
  StopOutlined, 
  EditOutlined,
  DeleteOutlined,
  VideoCameraOutlined
} from '@ant-design/icons';
import { useState, useMemo } from 'react';
import { useQueryClient } from '@tanstack/react-query';
import type { Device } from '@/types/device';
import { useAddGB28181Device, useDeleteGB28181Device, useStartDeviceStream } from '@/hooks/useDevices';
import { useStreams, useStopStream } from '@/hooks/useStreams';
import { stopGB28181DeviceStream } from '@/api/devices';

type OutputProtocol = 'http-flv' | 'hls';  // GB28181只支持HLS和FLV

interface GB28181DeviceCardProps {
  device: Device;
  onRefresh?: () => void;
}

export default function GB28181DeviceCard({ device, onRefresh }: GB28181DeviceCardProps) {
  const [editModalOpen, setEditModalOpen] = useState(false);
  const [detailModalOpen, setDetailModalOpen] = useState(false);
  const [streamModalOpen, setStreamModalOpen] = useState(false);
  const [outputProtocol, setOutputProtocol] = useState<OutputProtocol>('http-flv');
  const [selectedChannel, setSelectedChannel] = useState<string>(device.device_id); // 默认使用设备ID作为通道ID
  const [form] = Form.useForm();
  const queryClient = useQueryClient();

  const { data: allStreams = [] } = useStreams();
  const startDeviceStream = useStartDeviceStream('gb28181');
  const stopStream = useStopStream();
  const addDeviceMutation = useAddGB28181Device();
  const deleteDeviceMutation = useDeleteGB28181Device();

  // 查找该设备的所有流
  const deviceStreams = useMemo(() => {
    return allStreams.filter(s => {
      if (!s.source_url) return false;
      // 匹配 gb28181://device_id/channel_id 格式
      if (s.source_url.startsWith(`gb28181://${device.device_id}/`)) {
        return true;
      }
      return false;
    });
  }, [allStreams, device.device_id]);

  // 检查是否有运行中的流
  const hasRunningStream = useMemo(() => {
    return deviceStreams.some(s => 
      s.status === 2 || s.status === 'running' || 
      s.status_text === 'running' || s.status === 1 || s.status === 'starting'
    );
  }, [deviceStreams]);

  // 获取设备通道列表（如果有）
  const channels = useMemo(() => {
    // 如果设备有channels字段，使用它；否则使用设备ID作为默认通道
    const deviceChannels = device.channels || [];
    if (deviceChannels.length > 0) {
      return deviceChannels;
    }
    // 默认通道：使用设备ID
    return [device.device_id];
  }, [device]);

  const handleEdit = () => {
    form.setFieldsValue({
      id: device.device_id,
      name: device.name,
      ip: device.ip,
      port: device.port,
      manufacturer: device.manufacturer,
      model: device.model,
      username: (device as any).username || '',
      password: (device as any).password || '',
    });
    setEditModalOpen(true);
  };

  const handleEditSubmit = async () => {
    try {
      const values = await form.validateFields();
      await addDeviceMutation.mutateAsync({
        id: values.id,  // 后端期望的是 'id' 而不是 'device_id'
        name: values.name,
        ip: values.ip,
        port: values.port,
        manufacturer: values.manufacturer,
        model: values.model,
        protocol: 'gb28181',
        username: values.username,
        password: values.password,
      } as any);
      setEditModalOpen(false);
      if (onRefresh) {
        onRefresh();
      }
    } catch (error: any) {
      message.error(`更新设备失败: ${error.message}`);
    }
  };

  const handleDelete = async () => {
    try {
      await deleteDeviceMutation.mutateAsync(device.device_id);
      if (onRefresh) {
        onRefresh();
      }
    } catch (error: any) {
      message.error(`删除设备失败: ${error.message}`);
    }
  };

  const handleStartStream = async () => {
    try {
      const streamName = `gb28181_${device.device_id}_${selectedChannel}_${Date.now()}`;
      
      await startDeviceStream.mutateAsync({
        deviceId: device.device_id,
        channelId: selectedChannel,
        app: 'live',
        stream: streamName,
        outputProtocol: outputProtocol,
      });
      
      message.success('流启动成功');
      setStreamModalOpen(false);
      queryClient.invalidateQueries({ queryKey: ['streams'] });
      setTimeout(() => {
        queryClient.invalidateQueries({ queryKey: ['streams'] });
      }, 1000);
      if (onRefresh) {
        onRefresh();
      }
    } catch (error: any) {
      const errorMsg = error.message || String(error);
      if (errorMsg.includes('设备离线') || errorMsg.includes('DEVICE_OFFLINE')) {
        message.error({
          content: '设备离线，无法启动流。GB28181设备需要先通过SIP REGISTER注册才能启动流。',
          duration: 8,
        });
      } else if (errorMsg.includes('超时') || errorMsg.includes('timeout') || errorMsg.includes('未收到') || errorMsg.includes('Starting')) {
        message.error({
          content: '流启动超时。可能原因：1) 设备未响应INVITE请求；2) 设备模拟器未运行；3) 网络连接问题。请检查设备状态和模拟器日志。',
          duration: 10,
        });
      } else {
        message.error(`流启动失败: ${errorMsg}`);
      }
    }
  };

  const handleStopStream = async () => {
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
    queryClient.invalidateQueries({ queryKey: ['streams'] });
    if (onRefresh) {
      onRefresh();
    }
  };

  return (
    <>
      <Card
        title={
          <div style={{ display: 'flex', justifyContent: 'space-between', alignItems: 'center' }}>
            <span>{device.name || device.device_id}</span>
            <Space>
              <Tag color={device.online ? 'green' : 'red'}>
                {device.online ? '在线' : '离线'}
              </Tag>
              <Tag color="blue">GB28181</Tag>
            </Space>
          </div>
        }
        extra={
          <Space>
            <Button size="small" icon={<ReloadOutlined />} onClick={onRefresh}>
              刷新
            </Button>
            <Button size="small" icon={<EditOutlined />} onClick={handleEdit}>
              编辑
            </Button>
            <Popconfirm
              title="确定要删除此设备吗？"
              onConfirm={handleDelete}
              okText="确定"
              cancelText="取消"
            >
              <Button size="small" danger icon={<DeleteOutlined />}>
                删除
              </Button>
            </Popconfirm>
            <Button size="small" icon={<EyeOutlined />} onClick={() => setDetailModalOpen(true)}>
              详情
            </Button>
          </Space>
        }
        style={{ marginBottom: 16 }}
      >
        <Descriptions column={1} size="small">
          <Descriptions.Item label="设备ID">{device.device_id || '-'}</Descriptions.Item>
          <Descriptions.Item label="IP地址">{device.ip || '-'}</Descriptions.Item>
          <Descriptions.Item label="端口">{device.port || '-'}</Descriptions.Item>
          {device.manufacturer && (
            <Descriptions.Item label="制造商">{device.manufacturer}</Descriptions.Item>
          )}
          {device.model && (
            <Descriptions.Item label="型号">{device.model}</Descriptions.Item>
          )}
          <Descriptions.Item label="通道数">{channels.length}</Descriptions.Item>
          <Descriptions.Item label="运行中的流">
            {deviceStreams.filter(s => s.status === 2 || s.status === 'running' || s.status_text === 'running').length}
          </Descriptions.Item>
        </Descriptions>

        <div style={{ marginTop: 16 }}>
          {hasRunningStream ? (
            <Space direction="vertical" style={{ width: '100%' }}>
              <Button
                type="primary"
                danger
                icon={<StopOutlined />}
                onClick={handleStopStream}
                block
              >
                停止所有流
              </Button>
              <div style={{ fontSize: '12px', color: '#8c8c8c', textAlign: 'center' }}>
                已启动 {deviceStreams.filter(s => s.status === 2 || s.status === 'running' || s.status_text === 'running').length} 个流
              </div>
            </Space>
          ) : (
            <Button
              type="primary"
              icon={<VideoCameraOutlined />}
              onClick={() => setStreamModalOpen(true)}
              block
              disabled={!device.online}
            >
              {device.online ? '启动流' : '设备离线'}
            </Button>
          )}
        </div>
      </Card>

      {/* 编辑设备弹窗 */}
      <Modal
        title="编辑GB28181设备"
        open={editModalOpen}
        onOk={handleEditSubmit}
        onCancel={() => setEditModalOpen(false)}
        okText="保存"
        cancelText="取消"
        width={600}
      >
        <Form form={form} layout="vertical">
          <Form.Item name="id" label="设备ID" rules={[{ required: true, message: '请输入设备ID' }]}>
            <Input placeholder="20位国标ID" />
          </Form.Item>
          <Form.Item name="name" label="设备名称">
            <Input placeholder="设备名称" />
          </Form.Item>
          <Form.Item name="ip" label="IP地址" rules={[{ required: true, message: '请输入IP地址' }]}>
            <Input placeholder="192.168.1.100" />
          </Form.Item>
          <Form.Item name="port" label="SIP端口" rules={[{ required: true, message: '请输入端口' }]}>
            <Input type="number" placeholder="5060" />
          </Form.Item>
          <Form.Item name="manufacturer" label="制造商">
            <Input placeholder="制造商" />
          </Form.Item>
          <Form.Item name="model" label="型号">
            <Input placeholder="型号" />
          </Form.Item>
          <Form.Item name="username" label="用户名">
            <Input placeholder="SIP认证用户名" />
          </Form.Item>
          <Form.Item name="password" label="密码">
            <Input.Password placeholder="SIP认证密码" />
          </Form.Item>
        </Form>
      </Modal>

      {/* 启动流弹窗 */}
      <Modal
        title="启动GB28181流"
        open={streamModalOpen}
        onOk={handleStartStream}
        onCancel={() => setStreamModalOpen(false)}
        okText="启动"
        cancelText="取消"
        width={500}
        confirmLoading={startDeviceStream.isPending}
      >
        <Space direction="vertical" style={{ width: '100%' }} size="large">
          <div>
            <div style={{ marginBottom: 8, fontWeight: 500 }}>选择通道：</div>
            <Select
              value={selectedChannel}
              onChange={setSelectedChannel}
              style={{ width: '100%' }}
              options={channels.map((ch: string) => ({
                label: ch,
                value: ch,
              }))}
            />
          </div>
          <div>
            <div style={{ marginBottom: 8, fontWeight: 500 }}>选择输出协议：</div>
            <Select
              value={outputProtocol}
              onChange={(value) => setOutputProtocol(value)}
              style={{ width: '100%' }}
              options={[
                { label: 'HTTP-FLV (推荐)', value: 'http-flv' },
                { label: 'HLS', value: 'hls' },
              ]}
            />
            <div style={{ marginTop: 8, fontSize: '12px', color: '#8c8c8c' }}>
              {outputProtocol === 'http-flv' && 'HTTP-FLV：延迟低，兼容性好，推荐用于实时监控'}
              {outputProtocol === 'hls' && 'HLS：延迟较高，适合回放和点播'}
            </div>
          </div>
          {!device.online && (
            <div style={{ padding: '12px', background: '#fff7e6', borderRadius: '4px', border: '1px solid #ffd591' }}>
              <div style={{ fontSize: '12px', color: '#d46b08' }}>
                ⚠️ 设备当前离线，启动流可能会失败。设备需要先通过SIP REGISTER注册。
              </div>
            </div>
          )}
        </Space>
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
          <Descriptions.Item label="设备ID">{device.device_id || '-'}</Descriptions.Item>
          <Descriptions.Item label="设备名称">{device.name || '-'}</Descriptions.Item>
          <Descriptions.Item label="IP地址">{device.ip || '-'}</Descriptions.Item>
          <Descriptions.Item label="端口">{device.port || '-'}</Descriptions.Item>
          <Descriptions.Item label="制造商">{device.manufacturer || '-'}</Descriptions.Item>
          <Descriptions.Item label="型号">{device.model || '-'}</Descriptions.Item>
          <Descriptions.Item label="在线状态">
            <Tag color={device.online ? 'green' : 'red'}>
              {device.online ? '在线' : '离线'}
            </Tag>
          </Descriptions.Item>
          <Descriptions.Item label="通道列表">
            {channels.length > 0 ? (
              <Space wrap>
                {channels.map((ch: string) => (
                  <Tag key={ch}>{ch}</Tag>
                ))}
              </Space>
            ) : (
              '-'
            )}
          </Descriptions.Item>
          <Descriptions.Item label="运行中的流">
            {deviceStreams.filter(s => s.status === 2 || s.status === 'running' || s.status_text === 'running').length}
          </Descriptions.Item>
        </Descriptions>
      </Modal>
    </>
  );
}

