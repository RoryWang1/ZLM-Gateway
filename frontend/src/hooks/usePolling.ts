// 轮询 Hook

import { useEffect, useRef } from 'react';

interface UsePollingOptions {
  enabled?: boolean;
  interval?: number;
}

/**
 * 轮询 Hook
 */
export function usePolling(
  callback: () => void | Promise<void>,
  options: UsePollingOptions = {}
) {
  const { enabled = true, interval = 5000 } = options;
  const callbackRef = useRef(callback);

  useEffect(() => {
    callbackRef.current = callback;
  }, [callback]);

  useEffect(() => {
    if (!enabled) return;

    const execute = async () => {
      await callbackRef.current();
    };

    execute(); // 立即执行一次

    const timer = setInterval(execute, interval);

    return () => {
      clearInterval(timer);
    };
  }, [enabled, interval]);
}

