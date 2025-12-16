// 重构后的设备管理页面 - 使用协议适配器模式

import React, { useState } from 'react';
import { Typography, Row, Col, Empty, Spin, Button, Space, Tabs } from 'antd';
import { PlusOutlined } from '@ant-design/icons';
import { useProtocols, useProtocolDevices } from '@/hooks/useProtocol';
import { useStreamUpdates } from '@/hooks/useStreams';
import UnifiedDeviceDiscovery from '@/components/devices/UnifiedDeviceDiscovery';
import ProtocolDeviceCard from '@/components/devices/ProtocolDeviceCard';
import GB28181DeviceForm from '@/components/devices/GB28181DeviceForm';
import type { Device, DeviceProtocol } from '@/types/device';

const { Title } = Typography;

export default function DevicesV2() {
  const protocols = useProtocols();
  const [discoveredDevices, setDiscoveredDevices] = useState<Map<DeviceProtocol, Device[]>>(new Map());
  const [gb28181FormOpen, setGB28181FormOpen] = useState(false);
  const [activeTab, setActiveTab] = useState<string>('all');

  // 调试：检查protocols
  React.useEffect(() => {
    console.log('DevicesV2: protocols数量:', protocols.length);
    protocols.forEach(p => {
      console.log(`  - ${p.protocol}: ${p.config.label}`);
    });
  }, [protocols]);

  // 监听流状态更新
  useStreamUpdates();

  // 为每个协议预先定义hooks（必须在组件顶层调用，不能在循环或useMemo中调用）
  // 由于协议列表是固定的，我们可以为所有可能的协议定义hooks
  // 但只对实际启用的协议发起请求（通过enabled选项控制）
  const enabledProtocols = React.useMemo(() => {
    return new Set(protocols.map(p => p.protocol));
  }, [protocols]);

  const onvifQuery = useProtocolDevices('onvif');
  const isapiQuery = useProtocolDevices('isapi');
  const dahuaQuery = useProtocolDevices('dahua');
  const psiaQuery = useProtocolDevices('psia');
  const localCameraQuery = useProtocolDevices('local-camera');
  const gb28181Query = useProtocolDevices('gb28181');

  // 创建协议查询映射
  const protocolDeviceQueries = React.useMemo(() => {
    const queryMap = new Map<DeviceProtocol, ReturnType<typeof useProtocolDevices>>();
    queryMap.set('onvif', onvifQuery);
    queryMap.set('isapi', isapiQuery);
    queryMap.set('dahua', dahuaQuery);
    queryMap.set('psia', psiaQuery);
    queryMap.set('local-camera', localCameraQuery);
    queryMap.set('gb28181', gb28181Query);

    // 根据protocols数组创建查询列表
    return protocols.map(adapter => ({
      protocol: adapter.protocol,
      query: queryMap.get(adapter.protocol)!,
    }));
  }, [protocols, onvifQuery, isapiQuery, dahuaQuery, psiaQuery, localCameraQuery, gb28181Query]);

  const handleDiscover = (devices: Device[], protocol: DeviceProtocol) => {
    setDiscoveredDevices(prev => {
      const newMap = new Map(prev);
      // 获取当前协议已存在的设备（包括从API获取的和之前发现的）
      const query = protocolDeviceQueries.find(q => q.protocol === protocol)?.query;
      const apiDevices = query?.data || [];
      const existingDiscovered = newMap.get(protocol) || [];
      const allExisting = [...apiDevices, ...existingDiscovered];
      
      // 去重合并：只添加不在现有列表中的新设备
      const existingIds = new Set(allExisting.map(d => d.device_id));
      const uniqueNew = devices.filter(d => !existingIds.has(d.device_id));
      
      // 对于本地摄像头，发现后直接替换（因为发现就是刷新）
      if (protocol === 'local-camera') {
        newMap.set(protocol, devices);
      } else {
        newMap.set(protocol, [...existingDiscovered, ...uniqueNew]);
      }
      return newMap;
    });
  };


  // 构建标签页
  const tabItems = [
    {
      key: 'all',
      label: '全部设备',
      children: (
        <div>
          {(() => {
            // 检查是否有任何设备
            let hasAnyDevices = false;
            const protocolSections: React.ReactNode[] = [];
            
            protocols.forEach(adapter => {
              const protocol = adapter.protocol;
              const query = protocolDeviceQueries.find(q => q.protocol === protocol)?.query;
              const devices = query?.data || [];
              const discovered = discoveredDevices.get(protocol) || [];
              // 合并并去重：使用 device_id 作为唯一标识
              const deviceMap = new Map<string, Device>();
              devices.forEach(d => deviceMap.set(d.device_id, d));
              discovered.forEach(d => deviceMap.set(d.device_id, d));
              const allDevices = Array.from(deviceMap.values());

              if (allDevices.length > 0) {
                hasAnyDevices = true;
              }

              // 即使没有设备，也显示协议分组（特别是GB28181，需要显示"添加设备"按钮）
              // if (allDevices.length === 0 && !query?.isLoading) {
              //   return; // 跳过没有设备的协议
              // }

              protocolSections.push(
                <div key={protocol} style={{ marginBottom: 32 }}>
                  <div style={{ display: 'flex', justifyContent: 'space-between', alignItems: 'center', marginBottom: 16 }}>
                    <Title level={4} style={{ margin: 0 }}>
                      {adapter.config.label} ({allDevices.length})
                    </Title>
                    <Space>
                      {protocol === 'gb28181' && (
                        <Button
                          type="primary"
                          size="small"
                          icon={<PlusOutlined />}
                          onClick={() => setGB28181FormOpen(true)}
                        >
                          添加设备
                        </Button>
                      )}
                    </Space>
                  </div>
                  {query?.isLoading ? (
                    <div style={{ textAlign: 'center', padding: '40px' }}>
                      <Spin size="large" />
                    </div>
                  ) : allDevices.length === 0 ? (
                    <Empty
                      description={
                        protocol === 'gb28181'
                          ? "暂无GB28181设备，请点击'添加设备'按钮添加设备"
                          : `暂无${adapter.config.label}设备，请使用上方设备发现功能`
                      }
                    />
                  ) : (
                    <Row gutter={[16, 16]}>
                      {allDevices.map((device) => (
                        <Col key={`${protocol}-${device.device_id}`} xs={24} sm={12} lg={8} xl={6}>
                          <ProtocolDeviceCard device={device} />
                        </Col>
                      ))}
                    </Row>
                  )}
                </div>
              );
            });

            // 如果没有任何设备且所有查询都已完成，显示空状态
            if (!hasAnyDevices && protocolDeviceQueries.every(q => !q.query.isLoading)) {
              return (
                <Empty
                  description="暂无设备，请使用上方设备发现功能或添加设备"
                  style={{ marginTop: 40 }}
                />
              );
            }

            return protocolSections.length > 0 ? protocolSections : (
              <div style={{ textAlign: 'center', padding: '40px' }}>
                <Spin size="large" />
              </div>
            );
          })()}
        </div>
      ),
    },
    ...protocols.map(adapter => ({
      key: adapter.protocol,
      label: adapter.config.label,
      children: (
        <div>
          <div style={{ display: 'flex', justifyContent: 'space-between', alignItems: 'center', marginBottom: 16 }}>
            <Title level={4} style={{ margin: 0 }}>
              {adapter.config.label} 设备
            </Title>
            <Space>
              {adapter.protocol === 'gb28181' && (
                <Button
                  type="primary"
                  size="small"
                  icon={<PlusOutlined />}
                  onClick={() => setGB28181FormOpen(true)}
                >
                  添加设备
                </Button>
              )}
            </Space>
          </div>
          {(() => {
            const query = protocolDeviceQueries.find(q => q.protocol === adapter.protocol)?.query;
            const devices = query?.data || [];
            const discovered = discoveredDevices.get(adapter.protocol) || [];
            // 合并并去重：使用 device_id 作为唯一标识
            const deviceMap = new Map<string, Device>();
            devices.forEach(d => deviceMap.set(d.device_id, d));
            discovered.forEach(d => deviceMap.set(d.device_id, d));
            const allDevices = Array.from(deviceMap.values());

            if (query?.isLoading) {
              return (
                <div style={{ textAlign: 'center', padding: '40px' }}>
                  <Spin size="large" />
                </div>
              );
            }

            if (allDevices.length === 0) {
              return (
                <Empty
                  description={
                    adapter.protocol === 'gb28181'
                      ? "暂无GB28181设备，请点击'添加设备'按钮添加设备"
                      : `暂无${adapter.config.label}设备，请使用上方设备发现功能`
                  }
                />
              );
            }

            return (
              <Row gutter={[16, 16]}>
                {allDevices.map((device) => (
                    <Col key={`${adapter.protocol}-${device.device_id}`} xs={24} sm={12} lg={8} xl={6}>
                      <ProtocolDeviceCard device={device} />
                    </Col>
                ))}
              </Row>
            );
          })()}
        </div>
      ),
    })),
  ];

  return (
    <div>
      <div style={{ marginBottom: 16 }}>
        <Title level={2} style={{ margin: 0 }}>
          设备管理
        </Title>
      </div>

      <UnifiedDeviceDiscovery onDiscover={handleDiscover} />

      <Tabs
        activeKey={activeTab}
        onChange={setActiveTab}
        items={tabItems}
        type="card"
        style={{ marginTop: 24 }}
      />

      <GB28181DeviceForm
        open={gb28181FormOpen}
        onCancel={() => setGB28181FormOpen(false)}
        onSuccess={() => {
          const query = protocolDeviceQueries.find(q => q.protocol === 'gb28181');
          if (query) {
            query.query.refetch();
          }
        }}
      />
    </div>
  );
}

