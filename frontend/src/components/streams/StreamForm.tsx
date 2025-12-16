import { Modal, Form, Input, Select } from 'antd';
import { useEffect, useState } from 'react';
import type { StartStreamRequest, Protocol } from '@/types/stream';

interface StreamFormProps {
  open: boolean;
  onCancel: () => void;
  onOk: (values: StartStreamRequest) => Promise<void>;
  initialValues?: Partial<StartStreamRequest>;
}

const protocols: { label: string; value: Protocol }[] = [
  { label: 'RTSP', value: 'rtsp' },
  { label: 'RTMP', value: 'rtmp' },
  { label: 'HTTP-FLV', value: 'http-flv' },
  { label: 'DASH', value: 'dash' },
  { label: 'HLS', value: 'hls' },
  { label: 'QUIC', value: 'quic' },
];

export default function StreamForm({ open, onCancel, onOk, initialValues }: StreamFormProps) {
  const [form] = Form.useForm();
  const [loading, setLoading] = useState(false);
  const protocol = Form.useWatch('protocol', form);

  useEffect(() => {
    if (open) {
      // 重置表单并设置初始值
      form.resetFields();
      setLoading(false);
      // 设置默认值
      form.setFieldsValue({
        app: 'live',
        protocol: 'rtsp',
        output_protocol: 'http-flv',
        ...initialValues,
      });
    }
  }, [open, initialValues, form]);

  const handleOk = async () => {
    try {
      const values = await form.validateFields();
      setLoading(true);
      // 调用 onOk，但不等待它完成
      // onOk 会在收到 API 响应后立即关闭窗口
      onOk(values).finally(() => {
        setLoading(false);
        form.resetFields();
      });
    } catch (error) {
      console.error('Validation failed:', error);
      setLoading(false);
    }
  };

  const getPlaceholder = (p?: Protocol) => {
    switch (p) {
      case 'rtsp': return '例如: rtsp://example.com/stream';
      case 'rtmp': return '例如: rtmp://example.com/live/stream';
      case 'http-flv': return '例如: http://example.com/stream.flv';
      case 'dash': return '例如: http://example.com/manifest.mpd';
      case 'hls': return '例如: http://example.com/stream.m3u8';
      case 'quic': return '例如: quic://example.com/stream';
      default: return '例如: http://example.com/stream';
    }
  };

  return (
    <Modal
      title="启动流"
      open={open}
      onOk={handleOk}
      onCancel={onCancel}
      okText="启动"
      cancelText="取消"
      confirmLoading={loading}
      okButtonProps={{ disabled: loading }}
    >
      <Form form={form} layout="vertical">
        <Form.Item
          name="protocol"
          label="输入协议"
          rules={[{ required: true, message: '请选择输入协议' }]}
        >
          <Select placeholder="选择输入协议" options={protocols} />
        </Form.Item>
        <Form.Item
          name="output_protocol"
          label="输出协议"
          rules={[{ required: true, message: '请选择输出协议' }]}
          tooltip="选择 WebRTC 时，系统会自动转码音频为 G.711 以确保兼容性"
        >
          <Select
            placeholder="选择输出协议"
            options={[
              { label: 'HTTP-FLV', value: 'http-flv' },
              { label: 'HLS', value: 'hls' },
              { label: 'WebRTC', value: 'webrtc' },
            ]}
          />
        </Form.Item>
        <Form.Item
          name="source_url"
          label="源地址"
          rules={[{ required: true, message: '请输入源地址' }]}
        >
          <Input placeholder={getPlaceholder(protocol)} />
        </Form.Item>
        <Form.Item
          name="app"
          label="应用名"
          rules={[{ required: true, message: '请输入应用名' }]}
        >
          <Input placeholder="例如: live" />
        </Form.Item>
        <Form.Item
          name="stream"
          label="流名"
          rules={[{ required: true, message: '请输入流名' }]}
        >
          <Input placeholder="例如: test_stream" />
        </Form.Item>
      </Form>
    </Modal>
  );
}

