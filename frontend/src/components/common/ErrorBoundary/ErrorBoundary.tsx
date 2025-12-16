import { Component, ErrorInfo, ReactNode } from 'react';
import { Result, Button } from 'antd';

interface Props {
  children: ReactNode;
}

interface State {
  hasError: boolean;
  error: Error | null;
}

export default class ErrorBoundary extends Component<Props, State> {
  constructor(props: Props) {
    super(props);
    this.state = { hasError: false, error: null };
  }

  static getDerivedStateFromError(error: Error): State {
    return { hasError: true, error };
  }

  componentDidCatch(error: Error, errorInfo: ErrorInfo) {
    // 忽略 echarts-for-react 的 disconnect 错误
    const errorMessage = error.message || '';
    const errorStack = error.stack || '';
    const fullError = `${errorMessage} ${errorStack}`;
    
    const hasDisconnect = fullError.includes('disconnect');
    const hasUndefinedRead = fullError.includes('Cannot read properties of undefined');
    
    if (hasDisconnect && hasUndefinedRead) {
      // 静默忽略这个已知的库内部错误
      return;
    }
    
    console.error('ErrorBoundary caught an error:', error, errorInfo);
  }

  handleReset = () => {
    this.setState({ hasError: false, error: null });
  };

  render() {
    if (this.state.hasError) {
      return (
        <Result
          status="500"
          title="500"
          subTitle={this.state.error?.message || '抱歉，页面出现了错误'}
          extra={
            <Button type="primary" onClick={this.handleReset}>
              重试
            </Button>
          }
        />
      );
    }

    return this.props.children;
  }
}

