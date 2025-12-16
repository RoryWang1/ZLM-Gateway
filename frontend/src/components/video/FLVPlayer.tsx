// FLV 播放器组件

import { useEffect, useRef, useState } from 'react';
import flvjs from 'flv.js';
import { Spin, Alert, Button } from 'antd';
import { PlayCircleOutlined, PauseCircleOutlined } from '@ant-design/icons';

interface FLVPlayerProps {
  url: string;
  autoPlay?: boolean;
  controls?: boolean;
  style?: React.CSSProperties;
}

export default function FLVPlayer({
  url,
  autoPlay = true,
  controls = true,
  style,
}: FLVPlayerProps) {
  const videoRef = useRef<HTMLVideoElement>(null);
  const playerRef = useRef<flvjs.Player | null>(null);
  const loadingRef = useRef(true); // 使用 ref 来跟踪 loading 状态，避免闭包问题
  const lastJumpTimeRef = useRef(0); // 记录上次跳转时间，用于判断暂停是否由跳转引起
  const speedAdjustTimeoutRef = useRef<number | null>(null); // 记录加速播放的定时器
  const [loading, setLoading] = useState(true);
  const [error, setError] = useState<string | null>(null);
  const [isPlaying, setIsPlaying] = useState(false);
  const [showControls, setShowControls] = useState(false);

  useEffect(() => {
    // 抑制 flv.js 的一些不影响播放的警告
    const originalWarn = console.warn;
    const warnFilter = (message: any, ...args: any[]) => {
      if (typeof message === 'string') {
        // 抑制 MP4Remuxer 的警告（这些是 flv.js 的自动修复机制，不影响播放）
        if (message.includes('[MP4Remuxer]') && (
          message.includes('Dropping') ||
          message.includes('timestamp gap') ||
          message.includes('Silent frames')
        )) {
          return;
        }
        // 抑制 FlvPlayer 的 "stuck at 0" 警告（这是 flv.js 的自动恢复机制，不影响播放）
        if (message.includes('[FlvPlayer]') && message.includes('Playback seems stuck at 0')) {
          return;
        }
      }
      originalWarn(message, ...args);
    };
    console.warn = warnFilter;

    // 重置状态
    loadingRef.current = true;
    setLoading(true);
    setError(null);
    console.log(`[FLVPlayer] Mounting player for URL: ${url}`);

    if (!flvjs.isSupported()) {
      setError('浏览器不支持 FLV 播放');
      loadingRef.current = false;
      setLoading(false);
      return;
    }

    if (!videoRef.current) return;

    // 检测是否是FLV输入FLV输出流（本地流）
    // 对于这些流，使用更小的缓冲区配置，减少数据积累和传输速度
    // 注意：对于通过 ZLM 代理的 HTTP-FLV 流（URL 包含 /live/），应该启用跳转以获取最新数据
    const isLocalFLVStream = url.includes('localhost') || url.includes('127.0.0.1');
    const isZLMProxyStream = url.includes('/live/') && url.includes('.live.flv');
    const shouldDisableJump = isLocalFLVStream && !isZLMProxyStream;  // 对于本地FLV流但非ZLM代理流，禁用跳转

    // 清理旧的播放器实例
    if (playerRef.current) {
      try {
        playerRef.current.pause();
        playerRef.current.unload();
        playerRef.current.detachMediaElement();
        playerRef.current.destroy();
      } catch (e) {
        console.warn('Error destroying old FLV player:', e);
      }
      playerRef.current = null;
    }

    // 创建新的播放器实例 - 不预设hasAudio/hasVideo，让flv.js自动检测
    // 这样可以处理只有视频没有音频的流（如本地摄像头）
    // 对于FLV输入FLV输出流，使用更小的缓冲区配置，减少数据积累和传输速度
    const player = flvjs.createPlayer(
      {
        type: 'flv',
        url: url,
        isLive: true,
        // 不设置hasAudio和hasVideo，让flv.js根据metadata自动检测
      },
      {
        // 对于 live 流，flv.js 会自动跳转到最新数据
        // enableStashBuffer: true 允许 flv.js 缓冲数据并自动跳转到最新时间戳
        enableStashBuffer: shouldDisableJump ? false : true,  // 对于ZLM代理流，启用stash buffer以支持跳转到最新数据
        stashInitialSize: shouldDisableJump ? 16 : 128,  // 对于ZLM代理流，使用正常缓冲区大小
        lazyLoad: false,  // 关闭懒加载，确保立即开始加载数据
        lazyLoadMaxDuration: shouldDisableJump ? 0.1 : 1,  // 对于ZLM代理流，使用正常的懒加载持续时间
        enableWorker: false,
        autoCleanupSourceBuffer: true,
        fixAudioTimestampGap: true,  // 启用音频时间戳修复（即使没有音频也不会有问题）
        accurateSeek: false,  // 直播流不需要精确seek
        // 对于 live 流，flv.js 会自动检测时间戳跳转并跳转到最新数据
        // autoCleanupMaxBackwardDuration 和 autoCleanupMinBackwardDuration 控制向后清理的持续时间
        // 较大的值允许 flv.js 有更多时间检测和跳转到最新数据
        autoCleanupMaxBackwardDuration: shouldDisableJump ? 1 : 5,  // 对于ZLM代理流，使用更大的向后清理持续时间，允许跳转到最新数据
        autoCleanupMinBackwardDuration: shouldDisableJump ? 0.5 : 2,  // 对于ZLM代理流，使用更大的向后清理最小持续时间
      }
    );

    player.attachMediaElement(videoRef.current);

    // 在加载前检查视频元素是否已经出错
    if (videoRef.current.error) {
      console.error('[FLVPlayer] Video element already has error before load:', videoRef.current.error);
      setError(`视频错误: ${videoRef.current.error.message || '未知错误'}`);
      loadingRef.current = false;
      setLoading(false);
      return;
    }

    // 对于 live 流，flv.js 会自动跳转到最新数据
    // 但我们需要确保播放器配置正确，特别是 isLive: true
    // 对于 ZLM 代理的 live 流，flv.js 应该能够自动处理时间戳跳转
    player.load();

    playerRef.current = player;

    // 统一的播放就绪处理函数
    const handleReadyToPlay = () => {
      if (loadingRef.current && videoRef.current) {
        const readyState = videoRef.current.readyState;
        console.log(`[FLVPlayer] Ready state: ${readyState}, canplay: ${videoRef.current.readyState >= 2}`);
        
        // readyState >= 2 (HAVE_CURRENT_DATA) 表示有足够数据可以播放
        if (readyState >= 2) {
          loadingRef.current = false;
      setLoading(false);
      if (autoPlay && videoRef.current) {
        // 尝试自动播放，但不强制静音
        // 如果浏览器阻止自动播放，用户可以点击播放按钮
        const playPromise = videoRef.current.play();
        if (playPromise !== undefined) {
          playPromise.catch((e) => {
            // NotAllowedError: 浏览器阻止了自动播放，这是正常的
            // 用户需要点击播放按钮来开始播放
            if (e.name === 'NotAllowedError') {
              console.log('[FLVPlayer] 浏览器阻止了自动播放，请点击播放按钮');
              setIsPlaying(false);
            } else if (e.name !== 'AbortError') {
              console.error('[FLVPlayer] Auto play failed:', e);
            }
          });
        }
      }
        }
      }
    };

    const handleLoadedMetadata = () => {
      // 减少日志输出，只在关键状态变化时输出
      handleReadyToPlay();
    };

    const handleLoadedData = () => {
      // 减少日志输出，只在关键状态变化时输出
      handleReadyToPlay();
    };

    const handleCanPlay = () => {
      // 只在首次触发时输出日志
      if (loadingRef.current) {
        console.log('[FLVPlayer] 视频可以播放');
      }
      handleReadyToPlay();
    };

    const handlePlay = () => {
      setIsPlaying(true);
    };

    const handlePause = () => {
      // 检查是否是用户主动暂停（通过检查事件来源）
      // 如果是程序内部操作（如跳转）导致的暂停，不应该停止播放器
      const video = videoRef.current;
      if (!video) return;
      
      // 如果是因为跳转导致的短暂暂停，忽略它
      // 通过检查是否在跳转后的短时间内来判断
      const now = Date.now();
      const timeSinceLastJump = now - lastJumpTimeRef.current;
      if (timeSinceLastJump < 1000) {  // 增加到1秒，给跳转更多时间
        console.log(`[FLVPlayer] Ignoring pause event caused by buffer jump (${timeSinceLastJump}ms ago)`);
        // 检查视频是否真的被暂停了，如果是，需要恢复播放
        // 对于直播流，即使currentTime是0也可能是正常的（刚开始播放）
        // 所以只要视频被暂停且没有结束，且有缓冲区数据，就尝试恢复播放
        setTimeout(() => {
          if (video && video.paused && !video.ended) {
            // 检查是否有缓冲区数据，如果有数据说明流是正常的
            const buffered = video.buffered;
            const hasBuffer = buffered.length > 0 && buffered.end(buffered.length - 1) > 0;
            
            // 检查视频是否已经播放了一段时间（至少0.5秒），避免在刚开始播放时就恢复
            const minPlayTime = 0.5;
            const hasPlayedEnough = video.currentTime >= minPlayTime;
            
            if ((hasBuffer || hasPlayedEnough) && video.readyState >= 2) {
              console.log('[FLVPlayer] Video paused after jump, resuming playback (gentle resume)');
              video.play().catch((e) => {
                // AbortError 是正常的，当 play() 被 pause() 中断时会发生
                if (e.name !== 'AbortError') {
                  console.warn('[FLVPlayer] Failed to resume after jump:', e);
                }
              });
            } else {
              // 没有缓冲区或播放时间太短，可能是流有问题或刚开始，不恢复
              console.warn('[FLVPlayer] Video has no buffer or not played enough after jump, not resuming');
            }
          }
        }, 200);
        return;
      }
      
      console.log('[FLVPlayer] handlePause called (user action or real pause)');
      setIsPlaying(false);
      // 必须暂停FLV播放器以停止数据流
      // video.pause()只暂停播放，但flv.js可能继续推数据导致视频继续更新
      if (playerRef.current) {
        try {
          console.log('[FLVPlayer] Calling player.pause()');
          playerRef.current.pause();
          console.log('[FLVPlayer] Player paused successfully');
        } catch (e) {
          console.warn('[FLVPlayer] Error pausing player:', e);
        }
      } else {
        console.warn('[FLVPlayer] playerRef.current is null, cannot pause');
      }
    };

    // 添加视频元素错误处理
    const handleVideoError = (e: Event) => {
      const video = e.target as HTMLVideoElement;
      if (video.error) {
        console.error('[FLVPlayer] Video element error:', video.error);
        const errorCode = video.error.code;
        const errorMessage = video.error.message;

        // 立即停止播放器操作，避免继续追加数据到已出错的SourceBuffer
        if (playerRef.current) {
          try {
            playerRef.current.pause();
            playerRef.current.unload();
            playerRef.current.detachMediaElement();
            playerRef.current.destroy();
            playerRef.current = null;
          } catch (e) {
            console.warn('Error destroying player after video error:', e);
          }
        }

        // 处理不同的错误代码
        if (errorCode === MediaError.MEDIA_ERR_SRC_NOT_SUPPORTED) {
          setError('不支持的视频格式或流已停止');
        } else if (errorCode === MediaError.MEDIA_ERR_NETWORK) {
          setError('网络错误，请检查网络连接');
        } else if (errorCode === MediaError.MEDIA_ERR_DECODE) {
          setError('视频解码错误，可能是流格式问题');
        } else {
          setError(`视频错误: ${errorMessage || '未知错误'}`);
        }
        loadingRef.current = false;
        setLoading(false);
      }
    };

    const handleError = (errorType: string, errorDetail: string) => {
      console.error('FLV player error:', errorType, errorDetail);

      // 检查视频元素是否已经出错
      if (videoRef.current && videoRef.current.error) {
        console.error('[FLVPlayer] Video element error detected in handleError:', videoRef.current.error);
        const errorCode = videoRef.current.error.code;
        const errorMessage = videoRef.current.error.message;

        // 如果视频元素已经出错，停止所有操作
        if (playerRef.current) {
          try {
            playerRef.current.pause();
            playerRef.current.unload();
            playerRef.current.detachMediaElement();
            playerRef.current.destroy();
            playerRef.current = null;
          } catch (e) {
            console.warn('Error destroying player after video error:', e);
          }
        }

        // 根据错误代码设置错误消息
        if (errorCode === MediaError.MEDIA_ERR_SRC_NOT_SUPPORTED) {
          setError('不支持的视频格式或流已停止');
        } else if (errorCode === MediaError.MEDIA_ERR_NETWORK) {
          setError('网络错误，请检查网络连接');
        } else if (errorCode === MediaError.MEDIA_ERR_DECODE) {
          setError('视频解码错误，可能是流格式问题');
        } else {
          setError(`视频错误: ${errorMessage || '未知错误'}`);
        }
        loadingRef.current = false;
        setLoading(false);
        return;
      }

      // 处理 CodecUnsupported 错误 (例如 G.711 音频)
      if (errorType === 'MediaError' && errorDetail === 'CodecUnsupported') {
        console.warn('[FLVPlayer] 检测到不支持的编解码器，尝试禁用音频重试...');

        if (playerRef.current) {
          try {
            playerRef.current.pause();
            playerRef.current.unload();
            playerRef.current.detachMediaElement();
            playerRef.current.destroy();
            playerRef.current = null;

            // 重新创建播放器 - 强制禁用音频
            const newPlayer = flvjs.createPlayer({
              type: 'flv',
              url: url,
              isLive: true,
              hasAudio: false, // 强制禁用音频
              hasVideo: true,
            }, {
              enableStashBuffer: false,  // 重试时禁用stash buffer，减少数据积累
              stashInitialSize: 16,  // 极小的初始缓冲区（16KB）
              lazyLoad: false, // 重试时关闭懒加载以尽快恢复
              lazyLoadMaxDuration: 0.1,  // 极短的懒加载最大持续时间（0.1秒）
              enableWorker: false,
              autoCleanupSourceBuffer: true,
              fixAudioTimestampGap: false, // 没音频了不需要修复
              accurateSeek: false,
              autoCleanupMaxBackwardDuration: 1,
              autoCleanupMinBackwardDuration: 0.5,
            });

            if (videoRef.current) {
              newPlayer.attachMediaElement(videoRef.current);
              newPlayer.load();
              playerRef.current = newPlayer;

              // 重新绑定错误处理，避免无限递归（虽然 hasAudio: false 应该能解决）
              newPlayer.on(flvjs.Events.ERROR, (type: string, detail: string) => {
                // 如果再次发生 CodecUnsupported，那可能是视频编码也不支持，直接报错
                if (type === 'MediaError' && detail === 'CodecUnsupported') {
                  console.error('[FLVPlayer] 禁用音频后仍然不支持编解码器');
                  setError('不支持的视频编码');
                  loadingRef.current = false;
                  setLoading(false);
                } else {
                  handleError(type, detail);
                }
              });

              // 重新绑定其他事件
              newPlayer.on(flvjs.Events.MEDIA_INFO, () => {
                // 不再计算和显示延迟
              });

              setError(null); // 清除错误
              setLoading(true);

              // 关键修复：在绑定事件后才能安全地调用
              videoRef.current.load();
              if (autoPlay) {
                videoRef.current.muted = true;
                videoRef.current.play().catch((e) => {
                  if (e.name !== 'AbortError') {
                    console.error('[FLVPlayer] Retry play failed:', e);
                  }
                });
              }
              return;
            }
          } catch (e) {
            console.error('重试播放失败:', e);
            setError('播放器重试失败');
            loadingRef.current = false;
            setLoading(false);
            return;
          }
        }
      }

      // 处理不同类型的错误
      if (errorType === 'MediaError' && errorDetail === 'MediaMSEError') {
        // MSE错误，检查是否是视频元素错误导致的
        if (videoRef.current && videoRef.current.error) {
          console.error('[FLVPlayer] MSE error caused by video element error, stopping player');
          if (playerRef.current) {
            try {
              playerRef.current.pause();
              playerRef.current.unload();
              playerRef.current.detachMediaElement();
              playerRef.current.destroy();
              playerRef.current = null;
            } catch (e) {
              console.warn('Error destroying player:', e);
            }
          }
          setError('播放错误：视频元素已出错，请刷新页面重试');
          loadingRef.current = false;
          setLoading(false);
          return;
        }

        // MSE错误但视频元素正常，尝试重新加载
        console.warn('MSE错误，尝试重新加载播放器...');
        setTimeout(() => {
          if (playerRef.current && videoRef.current && !videoRef.current.error) {
            try {
              playerRef.current.pause();
              playerRef.current.unload();
              playerRef.current.detachMediaElement();
              playerRef.current.destroy();
              playerRef.current = null;

              // 重新创建播放器 - 不预设hasAudio/hasVideo
              const newPlayer = flvjs.createPlayer({
                type: 'flv',
                url: url,
                isLive: true,
                // 不设置hasAudio和hasVideo，让flv.js自动检测
              }, {
                enableStashBuffer: false,  // 禁用stash buffer，减少数据积累
                stashInitialSize: 16,  // 极小的初始缓冲区（16KB）
                lazyLoad: false,
                lazyLoadMaxDuration: 0.1,  // 极短的懒加载最大持续时间（0.1秒）
                enableWorker: false,
                autoCleanupSourceBuffer: true,
                fixAudioTimestampGap: true,
                accurateSeek: false,
                autoCleanupMaxBackwardDuration: 1,
                autoCleanupMinBackwardDuration: 0.5,
              });

              newPlayer.attachMediaElement(videoRef.current);
              newPlayer.load();
              playerRef.current = newPlayer;

              // 重新绑定错误监听
              newPlayer.on(flvjs.Events.ERROR, handleError);
              newPlayer.on(flvjs.Events.MEDIA_INFO, () => {
                // 不再计算和显示延迟
              });

              setError(null);
              setLoading(true);
            } catch (e) {
              console.error('重新加载播放器失败:', e);
              setError('播放错误，请刷新页面重试');
              loadingRef.current = false;
              setLoading(false);
            }
          } else {
            console.warn('[FLVPlayer] Cannot reload: video element has error or player is null');
            setError('播放错误，请刷新页面重试');
            loadingRef.current = false;
            setLoading(false);
          }
        }, 500);  // 提高监控频率到500ms
        return;
      }

      // 404 错误表示流不存在
      if (errorDetail && errorDetail.includes('404')) {
        setError('流不存在或已停止播放。请检查流状态。');
      } else if (errorDetail && errorDetail.includes('NetworkError')) {
        setError('网络错误，请检查网络连接');
      } else {
        setError(`播放错误: ${errorDetail}`);
      }
      loadingRef.current = false;
      setLoading(false);
    };

    videoRef.current.addEventListener('loadedmetadata', handleLoadedMetadata);
    videoRef.current.addEventListener('loadeddata', handleLoadedData);
    videoRef.current.addEventListener('canplay', handleCanPlay);
    videoRef.current.addEventListener('play', handlePlay);
    videoRef.current.addEventListener('pause', handlePause);
    videoRef.current.addEventListener('error', handleVideoError);
    
    // 监听视频结束事件（对于FLV输入FLV输出流，可能需要特殊处理）
    const handleVideoEnded = () => {
      console.warn('[FLVPlayer] Video ended event fired');
      // 对于直播流，ended事件不应该触发，如果触发了可能是流有问题
      // 检查是否是FLV输入FLV输出流（本地流）
      const isLocalFLVStream = url.includes('localhost') || url.includes('127.0.0.1');
      
      if (isLocalFLVStream && videoRef.current) {
        // 检查是否真的结束了（没有更多数据）
        const buffered = videoRef.current.buffered;
        const hasBuffer = buffered.length > 0 && buffered.end(buffered.length - 1) > videoRef.current.currentTime;
        
        if (!hasBuffer) {
          console.warn('[FLVPlayer] FLV input stream ended with no buffer, stream may have stopped');
          // 流可能真的结束了，不尝试恢复
        } else {
          // 有缓冲区但视频结束了，可能是MediaSource的问题
          console.log('[FLVPlayer] FLV input stream ended but has buffer, attempting to resume');
          setTimeout(() => {
            if (videoRef.current && videoRef.current.ended) {
              videoRef.current.play().catch((e) => {
                // AbortError 是正常的，当 play() 被 pause() 中断时会发生
                if (e.name !== 'AbortError') {
                  console.warn('[FLVPlayer] Failed to resume after ended:', e);
                }
              });
            }
          }, 100);
        }
      }
    };
    
    videoRef.current.addEventListener('ended', handleVideoEnded);
    player.on(flvjs.Events.ERROR, handleError);

    // 低延迟跳帧逻辑：如果缓冲区积压超过阈值就跳帧
    // 注意：对于FLV输入FLV输出的流，跳转可能导致MediaSource结束，所以禁用跳转
    // isLocalFLVStream 和 shouldDisableJump 已在上面定义
    
    const interval = setInterval(() => {
      if (videoRef.current && !videoRef.current.paused && videoRef.current.readyState >= 2) {
        const buffered = videoRef.current.buffered;
        if (buffered.length > 0) {
          const bufferedEnd = buffered.end(buffered.length - 1);
          const currentTime = videoRef.current.currentTime;
          const bufferAhead = bufferedEnd - currentTime;
          const now = Date.now();

          // 如果缓冲区积压超过阈值就跳帧
          // 对于FLV输入FLV输出的流，使用更高的阈值（5秒）和更长的最小播放时间（10秒）
          // 这样可以减少跳转频率，避免MediaSource结束
          const bufferThreshold = shouldDisableJump ? 5.0 : 1.0;  // FLV输入FLV输出使用5秒阈值
          const minPlayTimeBeforeJump = shouldDisableJump ? 10.0 : 3.0;  // FLV输入FLV输出需要播放10秒
          const videoPlayTime = videoRef.current.currentTime;
          
          if (bufferAhead > bufferThreshold && 
              (now - lastJumpTimeRef.current) > 2000 && 
              videoPlayTime >= minPlayTimeBeforeJump &&
              !shouldDisableJump) {  // 对于FLV输入FLV输出，完全禁用跳转
            console.log(`[FLVPlayer] Buffer too high (${bufferAhead.toFixed(2)}s), jumping to live edge (currentTime=${videoPlayTime.toFixed(2)}s)`);
            const targetTime = bufferedEnd - 0.2;  // 保留0.2秒安全缓冲
            
            // 记录跳转时间，用于在 handlePause 中判断
            // 在跳转前记录，确保 handlePause 能正确识别
            lastJumpTimeRef.current = now;
            
            // 执行跳转
            // 注意：不要立即调用 play()，让浏览器自然处理跳转
            // 如果视频正在播放，跳转后应该继续播放，不需要手动恢复
            // 调用 play() 可能导致视频重新开始播放（从0开始）
            videoRef.current.currentTime = targetTime;
            
            // 不在这里恢复播放，让浏览器自然处理跳转
            // 如果视频被暂停，handlePause 会处理（但会忽略跳转引起的暂停）
          } else if (shouldDisableJump && bufferAhead > bufferThreshold) {
            // 对于FLV输入FLV输出的流，不能跳转，但可以通过加速播放来消耗缓冲区
            // 这样可以减少延迟，同时避免MediaSource结束
            if ((now - lastJumpTimeRef.current) > 5000) {  // 每5秒检查一次
              console.log(`[FLVPlayer] Buffer too high (${bufferAhead.toFixed(2)}s) for FLV input stream, using playback speed adjustment`);
              lastJumpTimeRef.current = now;
              
              // 根据缓冲区大小调整播放速度
              // 缓冲区越大，加速越快，但不要超过1.2倍速（避免音调变化太明显）
              let speedMultiplier = 1.0;
              if (bufferAhead > 60.0) {
                speedMultiplier = 1.15; // 缓冲区超过60秒，加速15%
              } else if (bufferAhead > 30.0) {
                speedMultiplier = 1.10; // 缓冲区超过30秒，加速10%
              } else if (bufferAhead > 10.0) {
                speedMultiplier = 1.05; // 缓冲区超过10秒，加速5%
              }
              
              // 如果当前播放速度是1.0，应用加速
              if (videoRef.current && videoRef.current.playbackRate === 1.0 && speedMultiplier > 1.0) {
                videoRef.current.playbackRate = speedMultiplier;
                console.log(`[FLVPlayer] Increased playback rate to ${speedMultiplier.toFixed(2)}x to reduce buffer (${bufferAhead.toFixed(2)}s ahead)`);
                
                // 根据缓冲区大小调整加速持续时间
                // 缓冲区越大，加速时间越长
                const duration = bufferAhead > 60.0 ? 3000 : (bufferAhead > 30.0 ? 2000 : 1000);
                setTimeout(() => {
                  if (videoRef.current && videoRef.current.playbackRate !== 1.0) {
                    videoRef.current.playbackRate = 1.0;
                    console.log(`[FLVPlayer] Restored playback rate to 1.0x`);
                  }
                }, duration);
              }
            }
          }
        }
      }
    }, 2000);  // 每2秒检查一次，进一步减少性能开销

    const handleMediaInfo = () => {
      // 不再计算和显示延迟
    };

    player.on(flvjs.Events.MEDIA_INFO, handleMediaInfo);
    player.on(flvjs.Events.METADATA_ARRIVED, (meta: any) => {
      // 在追加数据前检查视频元素是否已出错
      if (videoRef.current && videoRef.current.error) {
        console.warn('[FLVPlayer] Video element has error, stopping metadata processing');
        if (playerRef.current) {
          try {
            playerRef.current.pause();
            playerRef.current.unload();
            playerRef.current.detachMediaElement();
            playerRef.current.destroy();
            playerRef.current = null;
          } catch (e) {
            console.warn('Error destroying player:', e);
          }
        }
        return;
      }

      console.log('[FLVPlayer] Metadata arrived:', meta);
      // 不再计算和显示延迟
      
      // 收到metadata后，检查是否可以播放
      setTimeout(() => {
        handleReadyToPlay();
      }, 100);
    });
    
    // 监听数据统计事件，检查是否有数据包到达
    // 只在播放开始时输出一次，之后只在出现问题时输出
    let hasLoggedPlaybackStart = false;
    player.on(flvjs.Events.STATISTICS_INFO, (info: any) => {
      if (videoRef.current) {
        const readyState = videoRef.current.readyState;
        const buffered = videoRef.current.buffered;
        const bufferedLength = buffered.length;
        
        // 只在首次开始播放时输出一次日志（减少日志输出）
        // 注意：readyState >= 4 (HAVE_FUTURE_DATA) 表示有足够数据可以流畅播放
        // 但不要输出"推流播放"这样的消息，避免用户困惑
        if (!hasLoggedPlaybackStart && readyState >= 4 && bufferedLength > 0) {
          hasLoggedPlaybackStart = true;
          // 不再输出日志，避免控制台噪音
        }
        
        // 如果readyState还是0但已经有统计数据，说明数据在传输但可能有问题
        if (readyState === 0 && info && (info.speed || info.droppedFrames)) {
          // 检查是否有视频数据但无法写入SourceBuffer
          if (info.videoInfo && !info.audioInfo) {
            console.warn('[FLVPlayer] 检测到只有视频没有音频的流，这可能导致播放问题');
          }
        }
      }
    });
    
    // 监听LOADING_COMPLETE事件，检查加载是否完成
    player.on(flvjs.Events.LOADING_COMPLETE, () => {
      console.log('[FLVPlayer] Loading complete event fired');
      setTimeout(() => {
        handleReadyToPlay();
      }, 100);
    });
    
    // 添加定期检查，确保在收到数据后能及时更新状态
    const readyCheckInterval = setInterval(() => {
      if (loadingRef.current && videoRef.current) {
        const readyState = videoRef.current.readyState;
        const networkState = videoRef.current.networkState;
        
        if (readyState >= 2) {
          handleReadyToPlay();
        } else if (readyState === 0 && networkState === HTMLMediaElement.NETWORK_IDLE) {
          // readyState=0且networkState=IDLE表示没有数据源或数据源已停止
          // 只在出现问题时输出警告
          console.warn('[FLVPlayer] readyState=0且networkState=IDLE，可能流没有数据或已停止');
        }
      }
    }, 500);

    return () => {
      // 恢复原始的 console.warn
      console.warn = originalWarn;

      clearInterval(interval);
      // 移除手动 off 调用，避免 destroy 冲突导致的 crash
      // player.off(flvjs.Events.MEDIA_INFO, handleMediaInfo);

      clearInterval(readyCheckInterval);
      
      // 清除加速播放的定时器
      if (speedAdjustTimeoutRef.current) {
        clearTimeout(speedAdjustTimeoutRef.current);
        speedAdjustTimeoutRef.current = null;
      }
      
      // 恢复播放速度
      if (videoRef.current && videoRef.current.playbackRate !== 1.0) {
        videoRef.current.playbackRate = 1.0;
      }

      if (videoRef.current) {
        videoRef.current.removeEventListener('loadedmetadata', handleLoadedMetadata);
        videoRef.current.removeEventListener('loadeddata', handleLoadedData);
        videoRef.current.removeEventListener('canplay', handleCanPlay);
        videoRef.current.removeEventListener('play', handlePlay);
        videoRef.current.removeEventListener('pause', handlePause);
        videoRef.current.removeEventListener('error', handleVideoError);
        videoRef.current.removeEventListener('ended', handleVideoEnded);
      }

      if (playerRef.current) {
        playerRef.current.pause();
        playerRef.current.unload();
        playerRef.current.detachMediaElement();
        playerRef.current.destroy();
        playerRef.current = null;
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
      onMouseEnter={() => controls && setShowControls(true)}
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
        onClick={handleTogglePlay}
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

