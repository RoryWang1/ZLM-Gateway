// 通用协议设备卡片组件 - 基于协议适配器

import { lazy, Suspense } from 'react';
import { Card } from 'antd';
import type { Device } from '@/types/device';
import DeviceCard from './DeviceCard'; // 复用通用设备卡片逻辑

interface ProtocolDeviceCardProps {
  device: Device;
  onRefresh?: () => void;
}

export default function ProtocolDeviceCard({ device, onRefresh }: ProtocolDeviceCardProps) {
  // 对于特殊协议，使用专门的组件
  if (device.protocol === 'gb28181') {
    // GB28181使用专门的组件（动态导入）
    const GB28181DeviceCard = lazy(() => import('./GB28181DeviceCard'));
    return (
      <Suspense fallback={<Card loading />}>
        <GB28181DeviceCard device={device} onRefresh={onRefresh} />
      </Suspense>
    );
  }

  // 其他协议使用通用组件
  return <DeviceCard device={device} onRefresh={onRefresh} />;
}
