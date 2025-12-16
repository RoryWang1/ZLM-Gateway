import { useState, useEffect, useRef } from 'react';
import { Typography, Row, Col, Empty, Spin, Divider, Button, Space } from 'antd';
import { ReloadOutlined, PlusOutlined } from '@ant-design/icons';
import { useONVIFDevices, useLocalCameras, useDiscoverLocalCameras, useDeviceUpdates, useRefreshLocalCameras, useGB28181Devices } from '@/hooks/useDevices';
import { useStreamUpdates } from '@/hooks/useStreams';
import DeviceDiscovery from '@/components/devices/DeviceDiscovery';
import DeviceCard from '@/components/devices/DeviceCard';
import GB28181DeviceCard from '@/components/devices/GB28181DeviceCard';
import GB28181DeviceForm from '@/components/devices/GB28181DeviceForm';
import type { Device } from '@/types/device';

const { Title } = Typography;

export default function Devices() {
  const { data: onvifDevices = [], isLoading: onvifLoading, refetch: refetchONVIF } = useONVIFDevices();
  const { data: localCameras = [], isLoading: localCamerasLoading, refetch: refetchLocalCameras } = useLocalCameras();
  const { data: gb28181Devices = [], isLoading: gb28181Loading, refetch: refetchGB28181 } = useGB28181Devices();
  const discoverLocalCameras = useDiscoverLocalCameras();
  const refreshLocalCamerasMutation = useRefreshLocalCameras();
  const [discoveredDevices, setDiscoveredDevices] = useState<Device[]>([]);
  const [gb28181FormOpen, setGB28181FormOpen] = useState(false);
  // 使用 ref 跟踪是否已经尝试过自动发现，避免无限循环
  const hasAutoDiscoveredRef = useRef(false);
  
  // 监听设备更新（通过 WebSocket）
  useDeviceUpdates();
  // 监听流状态更新（通过 WebSocket），确保按钮状态实时更新
  useStreamUpdates();

  // 页面加载时自动发现本地摄像头（仅执行一次）
  useEffect(() => {
    // 如果本地摄像头列表为空，且未加载中，且尚未尝试过自动发现，则自动发现
    if (localCameras.length === 0 && !localCamerasLoading && !hasAutoDiscoveredRef.current) {
      hasAutoDiscoveredRef.current = true;
      discoverLocalCameras.mutate(undefined);
    }
  }, [localCameras.length, localCamerasLoading, discoverLocalCameras]);

  const isLoading = onvifLoading || localCamerasLoading || gb28181Loading;
  // 本地摄像头单独处理，不与其他设备混合
  // GB28181设备单独显示
  const otherDevices = [...onvifDevices, ...discoveredDevices];
  
  const refetch = () => {
    refetchONVIF();
    refetchLocalCameras();
    refetchGB28181();
  };

  const handleDiscover = (newDevices: Device[]) => {
    setDiscoveredDevices((prev) => {
      // 去重：基于 device_id
      const existingIds = new Set(prev.map((d) => d.device_id));
      const uniqueNewDevices = newDevices.filter((d) => !existingIds.has(d.device_id));
      return [...prev, ...uniqueNewDevices];
    });
  };

  return (
    <div>
      <Title level={2} style={{ marginBottom: 16 }}>
        设备发现
      </Title>

      <DeviceDiscovery onDiscover={handleDiscover} />

      {/* 本地摄像头设备列表 - 单独显示 */}
      <div style={{ marginTop: 24 }}>
        <div style={{ display: 'flex', justifyContent: 'space-between', alignItems: 'center', marginBottom: 16 }}>
          <Title level={4} style={{ margin: 0 }}>
            本地摄像头设备 ({localCameras.length})
          </Title>
          <Space>
            <Button
              size="small"
              icon={<ReloadOutlined />}
              loading={refreshLocalCamerasMutation.isPending}
              onClick={() => refreshLocalCamerasMutation.mutate()}
            >
              刷新本地设备
            </Button>
          </Space>
        </div>
        {localCamerasLoading ? (
          <div style={{ textAlign: 'center', padding: '40px' }}>
            <Spin size="large" />
          </div>
        ) : localCameras.length === 0 ? (
          <Empty description="未发现本地摄像头设备，请点击上方'开始发现'按钮进行设备发现" />
        ) : (
          <Row gutter={[16, 16]}>
            {localCameras.map((device) => (
              <Col key={`${device.protocol}-${device.device_id}`} xs={24} sm={12} lg={8} xl={6}>
                <DeviceCard device={device} onRefresh={refetch} />
              </Col>
            ))}
          </Row>
        )}
      </div>

      {/* GB28181设备列表 - 单独显示 */}
      <div style={{ marginTop: 24 }}>
        <div style={{ display: 'flex', justifyContent: 'space-between', alignItems: 'center', marginBottom: 16 }}>
          <Title level={4} style={{ margin: 0 }}>
            GB28181设备 ({gb28181Devices.length})
          </Title>
          <Space>
            <Button
              type="primary"
              icon={<PlusOutlined />}
              onClick={() => setGB28181FormOpen(true)}
            >
              添加设备
            </Button>
            <Button
              size="small"
              icon={<ReloadOutlined />}
              loading={gb28181Loading}
              onClick={() => refetchGB28181()}
            >
              刷新设备
            </Button>
          </Space>
        </div>
        {gb28181Loading ? (
          <div style={{ textAlign: 'center', padding: '40px' }}>
            <Spin size="large" />
          </div>
        ) : gb28181Devices.length === 0 ? (
          <Empty description="暂无GB28181设备，请点击'添加设备'按钮添加设备" />
        ) : (
          <Row gutter={[16, 16]}>
            {gb28181Devices.map((device) => (
              <Col key={`${device.protocol}-${device.device_id}`} xs={24} sm={12} lg={8} xl={6}>
                <GB28181DeviceCard device={device} onRefresh={refetch} />
              </Col>
            ))}
          </Row>
        )}
      </div>

      {/* GB28181设备添加/编辑表单 */}
      <GB28181DeviceForm
        open={gb28181FormOpen}
        onCancel={() => setGB28181FormOpen(false)}
        onSuccess={() => {
          refetchGB28181();
        }}
      />

      {/* 其他设备列表 */}
      {otherDevices.length > 0 && (
        <>
          <Divider />
          <Title level={4} style={{ marginTop: 24, marginBottom: 16 }}>
            其他设备 ({otherDevices.length})
          </Title>
          <Row gutter={[16, 16]}>
            {otherDevices.map((device) => (
              <Col key={`${device.protocol}-${device.device_id}`} xs={24} sm={12} lg={8} xl={6}>
                <DeviceCard device={device} onRefresh={refetch} />
              </Col>
            ))}
          </Row>
        </>
      )}

      {/* 如果没有任何设备 */}
      {!isLoading && localCameras.length === 0 && gb28181Devices.length === 0 && otherDevices.length === 0 && (
        <Empty description="暂无设备，请先进行设备发现" />
      )}
    </div>
  );
}
