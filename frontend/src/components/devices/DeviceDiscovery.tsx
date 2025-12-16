// 设备发现组件

import { useState } from 'react';
import { Card, Form, Input, Select, Button, message } from 'antd';
import { SearchOutlined } from '@ant-design/icons';
import { useDiscoverDevices } from '@/hooks/useDevices';
import type { DeviceProtocol } from '@/types/device';

interface DeviceDiscoveryProps {
  onDiscover: (devices: any[]) => void;
}

export default function DeviceDiscovery({ onDiscover }: DeviceDiscoveryProps) {
  const [form] = Form.useForm();
  const [protocol, setProtocol] = useState<DeviceProtocol>('onvif');
  const discoverMutation = useDiscoverDevices();

  const handleDiscover = async () => {
    try {
      const values = await form.validateFields();
      const options: any = {};

      if (protocol === 'onvif') {
        options.timeoutSeconds = values.timeoutSeconds || 5;
      } else if (protocol === 'local-camera') {
        // 本地摄像头不需要额外参数
      } else {
        options.ipRange = values.ipRange || '192.168.1.0/24';
        options.port = values.port ? parseInt(values.port, 10) : 80;  // 转换为整数
        if (protocol === 'dahua' || protocol === 'psia') {
          options.username = values.username;
          options.password = values.password;
        }
      }

      // Debug: 打印实际发送的参数
      console.log('🔍 设备发现参数:', { protocol, options });
      console.log('📋 表单值:', values);

      const devices = await discoverMutation.mutateAsync({
        protocol,
        options,
      });

      message.success(`发现 ${devices.length} 个设备`);
      onDiscover(devices);
    } catch (error: any) {
      if (error.message) {
        message.error(error.message);
      }
    }
  };

  return (
    <Card title="设备发现" style={{ marginBottom: 16 }}>
      <Form form={form} layout="vertical">
        <Form.Item label="协议类型" required>
          <Select
            value={protocol}
            onChange={setProtocol}
            options={[
              { label: 'ONVIF', value: 'onvif' },
              { label: 'ISAPI', value: 'isapi' },
              { label: '大华', value: 'dahua' },
              { label: 'PSIA', value: 'psia' },
              { label: '本地摄像头', value: 'local-camera' },
            ]}
          />
        </Form.Item>

        {protocol === 'onvif' ? (
          <Form.Item
            name="timeoutSeconds"
            label="超时时间（秒）"
            initialValue={5}
            rules={[{ required: true, message: '请输入超时时间' }]}
          >
            <Input type="number" min={1} max={30} />
          </Form.Item>
        ) : protocol === 'local-camera' ? (
          <div style={{ padding: '16px', background: '#f5f5f5', borderRadius: '4px', marginBottom: '16px' }}>
            <div style={{ color: '#666', fontSize: '14px' }}>
              本地摄像头发现将扫描系统上连接的USB摄像头设备，无需额外配置。
            </div>
          </div>
        ) : (
          <>
            <Form.Item
              name="ipRange"
              label="IP 范围"
              initialValue="192.168.1.0/24"
              rules={[{ required: true, message: '请输入 IP 范围' }]}
            >
              <Input placeholder="例如: 192.168.1.0/24" id={`${protocol}-ipRange`} />
            </Form.Item>
            <Form.Item
              name="port"
              label="端口"
              initialValue={80}
              rules={[{ required: true, message: '请输入端口' }]}
            >
              <Input type="number" min={1} max={65535} id={`${protocol}-port`} />
            </Form.Item>
            {(protocol === 'dahua' || protocol === 'psia') && (
              <>
                <Form.Item
                  name="username"
                  label="用户名"
                  rules={[{ required: true, message: '请输入用户名' }]}
                >
                  <Input placeholder="例如: admin" />
                </Form.Item>
                <Form.Item
                  name="password"
                  label="密码"
                  rules={[{ required: true, message: '请输入密码' }]}
                >
                  <Input.Password placeholder="请输入密码" />
                </Form.Item>
              </>
            )}
          </>
        )}

        <Form.Item>
          <Button
            type="primary"
            icon={<SearchOutlined />}
            onClick={handleDiscover}
            loading={discoverMutation.isPending}
            block
          >
            开始发现
          </Button>
        </Form.Item>
      </Form>
    </Card>
  );
}

