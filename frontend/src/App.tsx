import { lazy } from 'react';
import { BrowserRouter, Routes, Route, Navigate } from 'react-router-dom';
import ErrorBoundary from './components/common/ErrorBoundary/ErrorBoundary';
import Layout from './components/common/Layout/Layout';

const Dashboard = lazy(() => import('./pages/Dashboard'));
const Streams = lazy(() => import('./pages/Streams'));
const VideoView = lazy(() => import('./pages/VideoView'));
const Devices = lazy(() => import('./pages/Devices'));
const DevicesV2 = lazy(() => import('./pages/DevicesV2'));


function App() {
  return (
    <ErrorBoundary>
      <BrowserRouter
        future={{
          v7_startTransition: true,
          v7_relativeSplatPath: true,
        }}
      >
        <Routes>
          <Route path="/" element={<Layout />}>
            <Route index element={<Navigate to="/dashboard" replace />} />
            <Route path="dashboard" element={<Dashboard />} />
            <Route path="streams" element={<Streams />} />
            <Route path="video" element={<VideoView />} />
            <Route path="devices" element={<DevicesV2 />} />
            <Route path="devices-old" element={<Devices />} />
            
          </Route>
        </Routes>
      </BrowserRouter>
    </ErrorBoundary>
  );
}

export default App;

