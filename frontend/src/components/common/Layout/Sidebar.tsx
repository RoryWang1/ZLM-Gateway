import { Layout, Menu } from 'antd';
import { useNavigate, useLocation } from 'react-router-dom';
import {
  DashboardOutlined,
  PlayCircleOutlined,
  VideoCameraOutlined,
  ApartmentOutlined,

} from '@ant-design/icons';

const { Sider } = Layout;

const menuItems = [
  {
    key: '/dashboard',
    icon: <DashboardOutlined />,
    label: '仪表板',
  },
  {
    key: '/streams',
    icon: <PlayCircleOutlined />,
    label: '流管理',
  },
  {
    key: '/video',
    icon: <VideoCameraOutlined />,
    label: '视频展示',
  },
  {
    key: '/devices',
    icon: <ApartmentOutlined />,
    label: '设备发现',
  },
];

export default function Sidebar() {
  const navigate = useNavigate();
  const location = useLocation();

  return (
    <Sider width={200} style={{ background: '#fff' }}>
      <Menu
        mode="inline"
        selectedKeys={[location.pathname]}
        style={{ height: '100%', borderRight: 0 }}
        items={menuItems}
        onClick={({ key }) => navigate(key)}
      />
    </Sider>
  );
}

