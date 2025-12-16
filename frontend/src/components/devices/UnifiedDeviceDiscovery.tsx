// 统一设备发现组件 - 支持所有协议的插件化发现

import { useState } from 'react';
import { Tabs, Card, Space, Button, Empty } from 'antd';
import { ReloadOutlined } from '@ant-design/icons';
import { protocolRegistry } from '@/protocols';
import ProtocolDiscoveryForm from './ProtocolDiscoveryForm';
import type { DeviceProtocol, Device } from '@/types/device';

interface UnifiedDeviceDiscoveryProps {
  onDiscover: (devices: Device[], protocol: DeviceProtocol) => void;
  defaultProtocol?: DeviceProtocol;
}

export default function UnifiedDeviceDiscovery({
  onDiscover,
  defaultProtocol = 'onvif',
}: UnifiedDeviceDiscoveryProps) {
  const [activeProtocol, setActiveProtocol] = useState<DeviceProtocol>(defaultProtocol);
  const [loading, setLoading] = useState<Record<DeviceProtocol, boolean>>({} as Record<DeviceProtocol, boolean>);

  const adapters = protocolRegistry.getAll();

  const handleDiscover = async (devices: Device[], protocol: DeviceProtocol) => {
    onDiscover(devices, protocol);
  };

  const handleTabChange = (key: string) => {
    setActiveProtocol(key as DeviceProtocol);
  };

  const tabItems = adapters.map(adapter => ({
    key: adapter.protocol,
    label: adapter.config.label,
    children: (
      <ProtocolDiscoveryForm
        protocol={adapter.protocol}
        onDiscover={(devices) => handleDiscover(devices, adapter.protocol)}
        loading={loading[adapter.protocol]}
      />
    ),
  }));

  return (
    <Card
      title="设备发现"
      style={{ marginBottom: 16 }}
    >
      {adapters.length === 0 ? (
        <Empty description="未注册任何协议适配器" />
      ) : (
        <Tabs
          activeKey={activeProtocol}
          onChange={handleTabChange}
          items={tabItems}
          type="card"
        />
      )}
    </Card>
  );
}

