import { Tag } from 'antd';
import type { Protocol, GatewayType } from '@/types/stream';
import { PROTOCOL_COLORS } from '@/utils/constants';

interface ProtocolBadgeProps {
  protocol: Protocol;
  gatewayType?: GatewayType;
  processingType?: string;
  showType?: boolean;
}

export default function ProtocolBadge({ protocol, gatewayType, processingType, showType = true }: ProtocolBadgeProps) {
  const getColor = () => {
    if (gatewayType === 'native' || (!gatewayType && ['rtsp', 'rtmp'].includes(protocol))) {
      return PROTOCOL_COLORS.native;
    } else if (gatewayType === 'device' || (!gatewayType && ['onvif', 'isapi', 'dahua', 'psia'].includes(protocol))) {
      return PROTOCOL_COLORS.device;
    } else {
      return PROTOCOL_COLORS['non-native'];
    }
  };

  const getTypeText = () => {
    if (processingType === '0') return '直接代理';
    if (processingType === '1') return '复制';
    if (processingType === '2') return '转码';

    if (gatewayType === 'native') {
      return '原生';
    } else if (gatewayType === 'device') {
      return '设备';
    } else {
      return '转换';
    }
  };

  return (
    <div style={{ display: 'flex', gap: '4px', alignItems: 'center' }}>
      <Tag color={getColor()}>{protocol.toUpperCase()}</Tag>
      {showType && gatewayType && (
        <Tag color={getColor()} style={{ fontSize: '11px' }}>
          {getTypeText()}
        </Tag>
      )}
    </div>
  );
}

