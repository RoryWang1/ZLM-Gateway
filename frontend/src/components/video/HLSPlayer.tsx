// HLS 播放器组件

import { useEffect, useRef, useState } from 'react';
import Hls from 'hls.js';
import { Spin, Alert, Button } from 'antd';
import { PlayCircleOutlined, PauseCircleOutlined } from '@ant-design/icons';

interface HLSPlayerProps {
  url: string;
  autoPlay?: boolean;
  style?: React.CSSProperties;
}

export default function HLSPlayer({
  url,
  autoPlay = true,
  style,
}: HLSPlayerProps) {
  const videoRef = useRef<HTMLVideoElement>(null);
  const hlsRef = useRef<Hls | null>(null);
  const [loading, setLoading] = useState(true);
  const [error, setError] = useState<string | null>(null);
  const [isPlaying, setIsPlaying] = useState(false);
  const [showControls, setShowControls] = useState(false);

  useEffect(() => {
    // 重置状态
    setLoading(true);
    setError(null);
    console.log(`[HLSPlayer] Mounting player for URL: ${url}`);

    if (!videoRef.current) return;

    // 清理旧的 HLS 实例
    if (hlsRef.current) {
      try {
        hlsRef.current.destroy();
      } catch (e) {
        console.warn('Error destroying old HLS player:', e);
      }
      hlsRef.current = null;
    }

    // 重置 video 元素
    if (videoRef.current) {
      videoRef.current.src = '';
      videoRef.current.load();
    }

    if (Hls.isSupported()) {
      const hls = new Hls({
        enableWorker: true,
        lowLatencyMode: false,  // 禁用低延迟模式（可能导致缓冲区不足）
        // 优化配置 - 减少缓冲区，提高实时性
        maxBufferLength: 2.0,  // 降低到2秒，确保低延迟
        maxMaxBufferLength: 4.0,  // 降低到4秒最大缓冲
        maxBufferSize: 30 * 1000 * 1000,
        maxBufferHole: 0.1,  // 减少缓冲区间隙容忍度
        highBufferWatchdogPeriod: 2.0,
        nudgeOffset: 0.1,
        nudgeMaxRetry: 5,
        fragLoadingTimeOut: 20000,
        manifestLoadingTimeOut: 10000,
        // 优化的清理配置
        maxStarvationDelay: 2.0,  // 降低到2.0秒
        maxLoadingDelay: 2.0,  // 降低到2.0秒
        // 优化的实时模式 (LL-HLS like)
        liveSyncDurationCount: 2,  // 保持2个片段同步
        liveMaxLatencyDurationCount: 4,  // 最大延迟4个片段
        // 低延迟优化
        liveDurationInfinity: false,
        liveBackBufferLength: 0,
        // 添加启动配置
        startFragPrefetch: true,  // 预取起始片段
        testBandwidth: true,  // 测试带宽
        progressive: false,  // 不使用渐进式加载
        // 添加CORS和错误处理配置
        xhrSetup: (xhr, url) => {
          // 设置CORS相关头部
          xhr.withCredentials = false;
        },
        // 添加错误恢复配置
        startLevel: -1,  // 自动选择最佳质量
        capLevelToPlayerSize: true,  // 根据播放器大小限制质量
        // 添加缓冲区管理配置
        abrEwmaDefaultEstimate: 500000,  // 默认估计码率500kbps
        abrBandWidthFactor: 0.95,  // 带宽因子
        abrBandWidthUpFactor: 0.7,  // 上行带宽因子
      });

      hls.loadSource(url);
      hls.attachMedia(videoRef.current);

      hlsRef.current = hls;

      // 监听HLS事件，等待有足够的数据再播放
      hls.on(Hls.Events.MANIFEST_PARSED, () => {
        console.log('[HLSPlayer] Manifest parsed, waiting for buffer...');
      });

      hls.on(Hls.Events.BUFFER_APPENDING, () => {
        console.log('[HLSPlayer] Buffer appending...');
      });

      hls.on(Hls.Events.BUFFER_APPENDED, () => {
        console.log('[HLSPlayer] Buffer appended');
      });

      const handleLoadedMetadata = () => {
        console.log('[HLSPlayer] Metadata loaded, checking buffer...');
        // 等待缓冲区有足够的数据再播放
        const checkBuffer = () => {
          if (videoRef.current && videoRef.current.buffered.length > 0) {
            const bufferedEnd = videoRef.current.buffered.end(videoRef.current.buffered.length - 1);
            const currentTime = videoRef.current.currentTime;
            const bufferAhead = bufferedEnd - currentTime;
            
            console.log('[HLSPlayer] Buffer check:', {
              bufferedEnd,
              currentTime,
              bufferAhead,
              bufferedLength: videoRef.current.buffered.length
            });

            // 如果缓冲区至少有2秒的数据，可以开始播放
            if (bufferAhead >= 2.0) {
              console.log('[HLSPlayer] Buffer sufficient, starting playback');
              setLoading(false);
              if (autoPlay) {
                videoRef.current?.play().catch((e) => {
                  // AbortError 是正常的，当 play() 被 pause() 中断时会发生
                  if (e.name !== 'AbortError') {
                    console.error('Auto play failed:', e);
                  }
                });
              }
            } else {
              // 缓冲区不足，继续等待
              console.log('[HLSPlayer] Buffer insufficient, waiting...', bufferAhead);
              setTimeout(checkBuffer, 100); // 100ms后再次检查
            }
          } else {
            // 还没有缓冲区，继续等待
            console.log('[HLSPlayer] No buffer yet, waiting...');
            setTimeout(checkBuffer, 100); // 100ms后再次检查
          }
        };

        // 延迟检查，给HLS一些时间填充缓冲区
        setTimeout(checkBuffer, 500);
      };

      const handlePlay = () => {
        setIsPlaying(true);
      };

      const handlePause = () => {
        setIsPlaying(false);
      };

      const handleError = (_event: any, data: any) => {
        console.error('[HLSPlayer] Error:', data);
        if (data.fatal) {
          switch (data.type) {
            case Hls.ErrorTypes.NETWORK_ERROR:
              // 检查是否是 404 错误
              if (data.details && data.details.includes('404')) {
                console.error('[HLSPlayer] 404错误，流不存在:', url);
                setError('流不存在或已停止播放。请检查流状态。');
              } else if (data.details && data.details.includes('CORS')) {
                console.error('[HLSPlayer] CORS错误:', url);
                setError('CORS错误：请检查ZLM服务器的CORS配置');
              } else {
                console.error('[HLSPlayer] 网络错误:', data.details, 'URL:', url);
                setError(`网络错误：${data.details || '请检查网络连接和ZLM服务器状态'}`);
              }
              break;
            case Hls.ErrorTypes.MEDIA_ERROR:
              console.error('[HLSPlayer] 媒体错误:', data.details);
              setError('媒体错误，尝试恢复...');
              hls.recoverMediaError();
              break;
            default:
              console.error('[HLSPlayer] 其他错误:', data.type, data.details);
              setError(`播放错误：${data.details || '未知错误'}`);
              hls.destroy();
              break;
          }
          setLoading(false);
        } else {
          // 非致命错误，只记录日志
          // bufferStalledError 是缓冲区停滞错误，通常是因为缓冲区太小
          if (data.details === 'bufferStalledError') {
            console.warn('[HLSPlayer] 缓冲区停滞错误（非致命）:', data.details, 'buffer:', data.buffer);
            // 不显示错误给用户，因为这是非致命错误，HLS会自动恢复
          } else {
            console.warn('[HLSPlayer] 非致命错误:', data.type, data.details);
          }
        }
      };

      videoRef.current.addEventListener('loadedmetadata', handleLoadedMetadata);
      videoRef.current.addEventListener('play', handlePlay);
      videoRef.current.addEventListener('pause', handlePause);
      hls.on(Hls.Events.ERROR, handleError);
    } else if (videoRef.current?.canPlayType('application/vnd.apple.mpegurl')) {
      // 原生 HLS 支持（Safari）
      const handleLoadedMetadata = () => {
        setLoading(false);
        if (autoPlay) {
          videoRef.current?.play().catch((e) => {
            // AbortError 是正常的，当 play() 被 pause() 中断时会发生
            if (e.name !== 'AbortError') {
              console.error('Auto play failed:', e);
            }
          });
        }
      };
      const handleError = (e: Event) => {
        const target = e.target as HTMLVideoElement;
        if (target && target.error) {
          const error = target.error;
          if (error && error.code === error.MEDIA_ERR_SRC_NOT_SUPPORTED) {
            setError('流不存在或已停止播放。请检查流状态。');
          } else {
            setError('HLS 播放错误');
          }
        } else {
          setError('HLS 播放错误');
        }
        setLoading(false);
      };
      const handlePlay = () => {
        setIsPlaying(true);
      };
      const handlePause = () => {
        setIsPlaying(false);
      };

      videoRef.current.src = url;
      videoRef.current.addEventListener('loadedmetadata', handleLoadedMetadata);
      videoRef.current.addEventListener('error', handleError);
      videoRef.current.addEventListener('play', handlePlay);
      videoRef.current.addEventListener('pause', handlePause);

      return () => {
        if (videoRef.current) {
          videoRef.current.removeEventListener('loadedmetadata', handleLoadedMetadata);
          videoRef.current.removeEventListener('error', handleError);
          videoRef.current.removeEventListener('play', handlePlay);
          videoRef.current.removeEventListener('pause', handlePause);
        }
      };
    } else {
      setError('浏览器不支持 HLS 播放');
      setLoading(false);
    }

    return () => {
      if (videoRef.current) {
        videoRef.current.removeEventListener('play', () => setIsPlaying(true));
        videoRef.current.removeEventListener('pause', () => setIsPlaying(false));
      }
      if (hlsRef.current) {
        hlsRef.current.destroy();
        hlsRef.current = null;
      }
    };
  }, [url, autoPlay]);

  if (error) {
    return (
      <Alert
        message="播放错误"
        description={error}
        type="error"
        showIcon
        style={style}
      />
    );
  }

  const handleTogglePlay = () => {
    if (videoRef.current) {
      if (isPlaying) {
        videoRef.current.pause();
      } else {
        videoRef.current.play().catch((e) => {
          // AbortError 是正常的，当 play() 被 pause() 中断时会发生
          if (e.name !== 'AbortError') {
            console.error('Play failed:', e);
          }
        });
      }
    }
  };

  return (
    <div
      style={{ position: 'relative', ...style }}
      onMouseEnter={() => setShowControls(true)}
      onMouseLeave={() => setShowControls(false)}
    >
      {loading && (
        <div
          style={{
            position: 'absolute',
            top: '50%',
            left: '50%',
            transform: 'translate(-50%, -50%)',
            zIndex: 10,
          }}
        >
          <Spin size="large" />
        </div>
      )}
      <video
        ref={videoRef}
        style={{ width: '100%', height: '100%', backgroundColor: '#000' }}
        muted={!autoPlay}
        playsInline
      />

      {/* 自定义控制栏：悬停时显示播放/暂停按钮 */}
      {showControls && (
        <div
          style={{
            position: 'absolute',
            bottom: 10,
            left: '50%',
            transform: 'translateX(-50%)',
            zIndex: 20,
            transition: 'opacity 0.3s ease',
          }}
        >
          <Button
            type="text"
            icon={isPlaying ? <PauseCircleOutlined /> : <PlayCircleOutlined />}
            onClick={handleTogglePlay}
            style={{
              color: 'white',
              fontSize: '32px',
              width: '48px',
              height: '48px',
              display: 'flex',
              alignItems: 'center',
              justifyContent: 'center',
              backgroundColor: 'rgba(0, 0, 0, 0.5)',
              border: 'none',
            }}
          />
        </div>
      )}
      <style>{`
        video::-webkit-media-controls {
          display: none !important;
        }
        video::-webkit-media-controls-enclosure {
          display: none !important;
        }
        video::-webkit-media-controls-panel {
          display: none !important;
        }
        video::-webkit-media-controls-play-button {
          display: none !important;
        }
        video::-webkit-media-controls-timeline {
          display: none !important;
        }
        video::-webkit-media-controls-current-time-display {
          display: none !important;
        }
        video::-webkit-media-controls-time-remaining-display {
          display: none !important;
        }
        video::-webkit-media-controls-mute-button {
          display: none !important;
        }
        video::-webkit-media-controls-volume-slider {
          display: none !important;
        }
        video::-webkit-media-controls-fullscreen-button {
          display: none !important;
        }
      `}</style>
    </div>
  );
}

