import { Layout, Typography } from 'antd';

const { Header: AntHeader } = Layout;
const { Title } = Typography;

export default function Header() {
  return (
    <AntHeader style={{ background: '#fff', padding: '0 24px', borderBottom: '1px solid #f0f0f0' }}>
      <Title level={4} style={{ margin: '16px 0', color: '#1890ff' }}>
        ZLM-Gateway 流媒体网关
      </Title>
    </AntHeader>
  );
}

