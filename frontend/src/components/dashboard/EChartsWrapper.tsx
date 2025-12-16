import { useEffect, useRef } from 'react';
import ReactECharts from 'echarts-for-react';
import type { EChartsOption } from 'echarts';

interface EChartsWrapperProps {
  option: EChartsOption;
  style?: React.CSSProperties;
  opts?: any;
  notMerge?: boolean;
  lazyUpdate?: boolean;
  onChartReady?: (chart: any) => void;
}

/**
 * ECharts 包装器组件
 * 使用 key 属性强制重新创建组件，避免 React 严格模式下的清理错误
 */
export default function EChartsWrapper({
  option,
  style,
  opts,
  notMerge,
  lazyUpdate,
  onChartReady,
}: EChartsWrapperProps) {
  const chartKeyRef = useRef(0);
  const mountedRef = useRef(false);

  useEffect(() => {
    mountedRef.current = true;
    return () => {
      mountedRef.current = false;
      // 组件卸载时增加 key，强制重新创建
      chartKeyRef.current += 1;
    };
  }, []);

  // 包装 onChartReady 以捕获可能的错误
  const safeOnChartReady = (chart: any) => {
    try {
      if (onChartReady && mountedRef.current) {
        onChartReady(chart);
      }
    } catch (error) {
      // 忽略错误
    }
  };

  return (
    <ReactECharts
      key={chartKeyRef.current}
      option={option}
      style={style}
      opts={opts}
      notMerge={notMerge}
      lazyUpdate={lazyUpdate}
      onChartReady={safeOnChartReady}
    />
  );
}

