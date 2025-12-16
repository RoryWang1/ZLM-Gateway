import { Suspense } from 'react';
import { Outlet } from 'react-router-dom';
import { Layout as AntLayout } from 'antd';
import Header from './Header';
import Sidebar from './Sidebar';
import Loading from '../Loading/Loading';

const { Content } = AntLayout;

export default function Layout() {
  return (
    <AntLayout style={{ minHeight: '100vh' }}>
      <Sidebar />
      <AntLayout>
        <Header />
        <Content style={{ margin: '16px', padding: '24px', background: '#fff', borderRadius: '8px' }}>
          <Suspense fallback={<Loading />}>
            <Outlet />
          </Suspense>
        </Content>
      </AntLayout>
    </AntLayout>
  );
}

