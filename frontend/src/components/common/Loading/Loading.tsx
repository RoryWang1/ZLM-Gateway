import { Spin } from 'antd';

interface LoadingProps {
  size?: 'small' | 'default' | 'large';
  tip?: string;
  fullScreen?: boolean;
}

export default function Loading({ size = 'default', tip = '加载中...', fullScreen = false }: LoadingProps) {
  const style = fullScreen
    ? {
        position: 'fixed' as const,
        top: 0,
        left: 0,
        right: 0,
        bottom: 0,
        display: 'flex',
        alignItems: 'center',
        justifyContent: 'center',
        backgroundColor: 'rgba(255, 255, 255, 0.8)',
        zIndex: 9999,
      }
    : {
        display: 'flex',
        alignItems: 'center',
        justifyContent: 'center',
        padding: '40px',
      };

  return (
    <div style={style}>
      {fullScreen ? (
        <Spin size={size} tip={tip} />
      ) : (
        <Spin size={size}>
          <div style={{ marginTop: 8 }}>{tip}</div>
        </Spin>
      )}
    </div>
  );
}

