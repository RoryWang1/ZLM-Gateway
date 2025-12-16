// 协议发现表单组件 - 基于协议适配器的动态表单

import { useState, useEffect } from 'react';
import { Card, Form, Input, Select, Button, message, Space, Typography } from 'antd';
import { SearchOutlined, InfoCircleOutlined } from '@ant-design/icons';
import { protocolRegistry } from '@/protocols';
import type { DeviceProtocol, Device } from '@/types/device';
import type { DiscoveryOptions } from '@/protocols/types';

const { Text } = Typography;

interface ProtocolDiscoveryFormProps {
  protocol: DeviceProtocol;
  onDiscover: (devices: Device[]) => void;
  onCancel?: () => void;
  loading?: boolean;
}

export default function ProtocolDiscoveryForm({
  protocol,
  onDiscover,
  onCancel,
  loading = false,
}: ProtocolDiscoveryFormProps) {
  const [form] = Form.useForm();
  const adapter = protocolRegistry.get(protocol);

  if (!adapter) {
    return (
      <Card>
        <div style={{ textAlign: 'center', padding: '40px', color: '#8c8c8c' }}>
          协议 {protocol} 未注册
        </div>
      </Card>
    );
  }

  const config = adapter.config;

  // 初始化表单默认值
  useEffect(() => {
    const initialValues: any = {};
    config.formFields?.forEach(field => {
      if (field.defaultValue !== undefined) {
        initialValues[field.name] = field.defaultValue;
      }
    });
    form.setFieldsValue(initialValues);
  }, [protocol, form, config]);

  const handleDiscover = async () => {
    try {
      const values = await form.validateFields();
      const options: DiscoveryOptions = {};

      // 根据表单字段构建选项
      config.formFields?.forEach(field => {
        if (values[field.name] !== undefined) {
          options[field.name] = values[field.name];
        }
      });

      const devices = await adapter.discover(options);
      message.success(`发现 ${devices.length} 个设备`);
      onDiscover(devices);
    } catch (error: any) {
      if (error.errorFields) {
        // 表单验证错误
        return;
      }
      message.error(`设备发现失败: ${error.message}`);
    }
  };

  return (
    <Card
      title={
        <Space>
          <span>{config.label} 设备发现</span>
          {config.description && (
            <Text type="secondary" style={{ fontSize: '12px' }}>
              <InfoCircleOutlined /> {config.description}
            </Text>
          )}
        </Space>
      }
      style={{ marginBottom: 16 }}
    >
      <Form form={form} layout="vertical">
        {config.formFields && config.formFields.length > 0 ? (
          config.formFields.map(field => (
            <Form.Item
              key={field.name}
              name={field.name}
              label={field.label}
              rules={field.rules}
              help={field.helpText}
            >
              {field.type === 'select' ? (
                <Select
                  placeholder={field.placeholder}
                  options={field.options}
                />
              ) : field.type === 'password' ? (
                <Input.Password placeholder={field.placeholder} id={`${protocol}-${field.name}`} />
              ) : (
                <Input
                  type={field.type}
                  placeholder={field.placeholder}
                  min={field.type === 'number' ? 1 : undefined}
                  max={field.type === 'number' ? 65535 : undefined}
                  id={`${protocol}-${field.name}`}
                />
              )}
            </Form.Item>
          ))
        ) : (
          <div style={{ padding: '16px', background: '#f5f5f5', borderRadius: '4px', marginBottom: '16px' }}>
            <div style={{ color: '#666', fontSize: '14px' }}>
              {config.description || '此协议无需额外配置'}
            </div>
          </div>
        )}

        <Form.Item>
          <Space style={{ width: '100%', justifyContent: 'flex-end' }}>
            {onCancel && (
              <Button onClick={onCancel}>
                取消
              </Button>
            )}
            <Button
              type="primary"
              icon={<SearchOutlined />}
              onClick={handleDiscover}
              loading={loading}
              block={!onCancel}
            >
              开始发现
            </Button>
          </Space>
        </Form.Item>
      </Form>
    </Card>
  );
}

