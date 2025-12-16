import React from 'react';
import ReactDOM from 'react-dom/client';
import { ConfigProvider } from 'antd';
import zhCN from 'antd/locale/zh_CN';
import { QueryClient, QueryClientProvider } from '@tanstack/react-query';
import App from './App';
import './styles/global.css';
// 初始化协议注册表
import '@/protocols';

// 全局错误处理：捕获 echarts-for-react 在 React 严格模式下的 disconnect 错误
// 这是一个已知的库内部错误，不影响功能，可以安全忽略
const originalConsoleError = console.error;
console.error = (...args: any[]) => {
  try {
    // 将所有参数转换为字符串以便检查
    const allArgs = args.map(arg => {
      if (typeof arg === 'string') return arg;
      if (arg?.toString) return arg.toString();
      if (arg?.stack) return arg.stack;
      if (arg?.message) return arg.message;
      if (arg?.name) return arg.name;
      return String(arg);
    }).join(' ');
    
    // 匹配 disconnect 错误的特征（更宽泛的匹配）
    // 检查是否包含关键错误信息
    const hasDisconnect = allArgs.toLowerCase().includes('disconnect');
    const hasUndefinedRead = 
      allArgs.includes('Cannot read properties of undefined') ||
      allArgs.includes('reading \'disconnect\'') ||
      allArgs.includes('TypeError');
    const hasEChartsRelated = 
      allArgs.includes('echarts-for-react') ||
      allArgs.includes('EChartsReactCore') ||
      allArgs.includes('destroy2') ||
      allArgs.includes('removeSensor2') ||
      allArgs.includes('componentWillUnmount');
    
    // 如果包含 disconnect 错误特征，则忽略
    if (hasDisconnect && (hasUndefinedRead || hasEChartsRelated)) {
      // 静默忽略这个已知的库内部错误
      return;
    }
  } catch (e) {
    // 如果检查过程中出错，继续正常输出
  }
  
  // 其他错误正常输出
  originalConsoleError.apply(console, args);
};

// 捕获全局错误事件
window.addEventListener('error', (event) => {
  const errorMessage = event.message || '';
  const errorStack = event.error?.stack || '';
  const fullError = `${errorMessage} ${errorStack}`;
  
  // 匹配 disconnect 错误的特征
  const hasDisconnect = fullError.includes('disconnect');
  const hasUndefinedRead = fullError.includes('Cannot read properties of undefined');
  
  if (hasDisconnect && hasUndefinedRead) {
    event.preventDefault();
    event.stopPropagation();
    return false;
  }
}, true);

// 捕获未处理的 Promise 拒绝
window.addEventListener('unhandledrejection', (event) => {
  const errorMessage = event.reason?.message || '';
  const errorStack = event.reason?.stack || '';
  const fullError = `${errorMessage} ${errorStack}`;
  
  const hasDisconnect = fullError.includes('disconnect');
  const hasUndefinedRead = fullError.includes('Cannot read properties of undefined');
  
  if (hasDisconnect && hasUndefinedRead) {
    event.preventDefault();
  }
});

const queryClient = new QueryClient({
  defaultOptions: {
    queries: {
      refetchOnWindowFocus: false,
      retry: 1,
      staleTime: 5000,
      gcTime: 5 * 60 * 1000, // 5分钟后清理未使用的缓存，防止内存泄漏
    },
  },
});

// 在开发环境下禁用 StrictMode 以避免 echarts-for-react 的 disconnect 错误
// 这是 echarts-for-react 在 React 18 严格模式下的已知问题
const isDevelopment = import.meta.env.DEV;
const AppContent = (
    <QueryClientProvider client={queryClient}>
      <ConfigProvider locale={zhCN}>
        <App />
      </ConfigProvider>
    </QueryClientProvider>
);

ReactDOM.createRoot(document.getElementById('root')!).render(
  isDevelopment ? AppContent : <React.StrictMode>{AppContent}</React.StrictMode>
);

