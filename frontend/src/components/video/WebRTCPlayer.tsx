import { useEffect, useRef, useState } from 'react';
import { Spin, Alert, Button } from 'antd';
import { PlayCircleOutlined, PauseCircleOutlined } from '@ant-design/icons';

interface WebRTCPlayerProps {
    url: string;
    className?: string;
    autoPlay?: boolean;
    muted?: boolean;
    controls?: boolean;
}

export default function WebRTCPlayer({
    url,
    className,
    autoPlay = true,
    muted = false,  // 默认不静音，允许播放音频
    controls = true,
}: WebRTCPlayerProps) {
    const videoRef = useRef<HTMLVideoElement>(null);
    const playerRef = useRef<any>(null);
    const [loading, setLoading] = useState(true);
    const [error, setError] = useState<string | null>(null);
    const [isPlaying, setIsPlaying] = useState(false);
    const [showControls, setShowControls] = useState(false);
    const [needsUserInteraction, setNeedsUserInteraction] = useState(false);

    useEffect(() => {
        if (!url || !videoRef.current) return;

        // 确保 ZLMRTCClient 已加载
        if (!window.ZLMRTCClient) {
            setError('WebRTC 客户端库未加载');
            setLoading(false);
            return;
        }

        try {
            setLoading(true);
            setError(null);

            // 销毁旧实例
            if (playerRef.current) {
                playerRef.current.close();
                playerRef.current = null;
            }

            console.log(`[WebRTCPlayer] Mounting player for URL: ${url}`);
            console.log('Initializing WebRTC player with URL:', url);

            // 创建新实例
            // 注意：添加 forceTcp 选项以避免 ZLMRTCClient 内部访问 null 的错误
            playerRef.current = new window.ZLMRTCClient.Endpoint({
                element: videoRef.current,
                debug: true,
                zlmsdpUrl: url,
                simulcast: false,
                useCamera: false,
                audioEnable: true,
                videoEnable: true,
                recvOnly: true,
                forceTcp: false,  // 明确设置 forceTcp，避免 ZLMRTCClient 内部访问 null
            });

            // 监听 ZLMRTCClient 的远程流事件
            let handleRemoteStream: ((stream: MediaStream) => void) | null = null;
            if (playerRef.current) {
                handleRemoteStream = (stream: MediaStream) => {
                    console.log('[WebRTCPlayer] 收到远程流事件:', {
                        id: stream.id,
                        videoTracks: stream.getVideoTracks().length,
                        audioTracks: stream.getAudioTracks().length,
                    });
                    
                    // 强制设置视频元素的 srcObject
                    if (videoRef.current) {
                        console.log('[WebRTCPlayer] 强制设置视频元素的 srcObject');
                        videoRef.current.srcObject = stream;
                        
                        // 立即检查并尝试播放
                        setTimeout(() => {
                            if (videoRef.current && videoRef.current.srcObject) {
                                const currentStream = videoRef.current.srcObject as MediaStream;
                                const videoTracks = currentStream.getVideoTracks();
                                console.log('[WebRTCPlayer] 检查视频元素状态:', {
                                    hasSrcObject: !!videoRef.current.srcObject,
                                    videoTracks: videoTracks.length,
                                    readyState: videoRef.current.readyState,
                                    paused: videoRef.current.paused,
                                });
                                
                                // 如果视频轨道存在但被静音，尝试重新设置流
                                if (videoTracks.length > 0 && videoTracks[0].muted) {
                                    console.warn('[WebRTCPlayer] 视频轨道被静音，尝试重新设置流');
                                    // 创建一个新的流，包含相同的轨道
                                    const newStream = new MediaStream(videoTracks);
                                    // 添加音频轨道（如果有）
                                    currentStream.getAudioTracks().forEach(track => {
                                        newStream.addTrack(track);
                                    });
                                    videoRef.current.srcObject = newStream;
                                }
                                
                                // 尝试播放
                                if (autoPlay && videoRef.current.paused) {
                                    console.log('[WebRTCPlayer] 尝试播放视频');
                                    videoRef.current.play().catch((e: any) => {
                                        if (e.name !== 'AbortError') {
                                            console.error('[WebRTCPlayer] 播放失败:', e);
                                        }
                                    });
                                }
                            }
                        }, 100);
                    }
                    
                    // 检查视频轨道
                    const videoTracks = stream.getVideoTracks();
                    if (videoTracks.length > 0) {
                        console.log('[WebRTCPlayer] 远程流有视频轨道:', videoTracks.length);
                        videoTracks.forEach(track => {
                            console.log('[WebRTCPlayer] 视频轨道:', {
                                id: track.id,
                                enabled: track.enabled,
                                muted: track.muted,
                                readyState: track.readyState,
                            });
                            if (!track.enabled) {
                                track.enabled = true;
                            }
                        });
                    }
                };
                
                // 监听 WEBRTC_ON_REMOTE_STREAMS 事件（使用字符串常量）
                const eventName = 'WEBRTC_ON_REMOTE_STREAMS';
                if (playerRef.current.on) {
                    (playerRef.current as any).on(eventName, handleRemoteStream);
                }
                
                // 也检查是否已经有远程流（可能事件已经触发）
                if ((playerRef.current as any).remoteStream) {
                    console.log('[WebRTCPlayer] ZLMRTCClient 已有远程流，立即设置');
                    handleRemoteStream((playerRef.current as any).remoteStream);
                }
            }

            // 注意：RTCPeerConnection 的配置只能在创建时设置，不能在连接建立后修改
            // 因此我们不再尝试在连接建立后调用 setConfiguration
            // 如果需要配置，应该在创建 ZLMRTCClient.Endpoint 时通过配置选项设置
            
            // 监听轨道事件（用于调试和确保轨道启用）
            if (playerRef.current?.pc) {
                const pc = playerRef.current.pc;
                const handleTrack = (event: RTCTrackEvent) => {
                    const track = event.track;
                    const stream = event.streams[0];
                    console.log(`[WebRTCPlayer] 收到 ${track.kind} 轨道:`, {
                        id: track.id,
                        kind: track.kind,
                        enabled: track.enabled,
                        muted: track.muted,
                        readyState: track.readyState,
                        hasStream: !!stream,
                        streamId: stream?.id,
                    });
                    
                    // 如果 ZLMRTCClient 没有设置流，我们手动设置
                    if (track.kind === 'video' && videoRef.current) {
                        if (!videoRef.current.srcObject && stream) {
                            console.log('[WebRTCPlayer] 视频元素没有 srcObject，从 track 事件设置流');
                                videoRef.current.srcObject = stream;
                        } else if (!videoRef.current.srcObject) {
                            // 如果没有 stream，创建一个新的
                            console.log('[WebRTCPlayer] 创建新的 MediaStream 并添加视频轨道');
                                const newStream = new MediaStream([track]);
                                videoRef.current.srcObject = newStream;
                        } else if (videoRef.current.srcObject) {
                            // 如果已经有流，确保轨道在流中
                            const currentStream = videoRef.current.srcObject as MediaStream;
                            const existingTracks = currentStream.getVideoTracks();
                            const hasTrack = existingTracks.some(t => t.id === track.id);
                            if (!hasTrack) {
                                console.log('[WebRTCPlayer] 添加视频轨道到现有流');
                                currentStream.addTrack(track);
                            }
                        }
                        
                        // 如果轨道被静音，尝试重新设置流来取消静音
                        if (track.muted && videoRef.current.srcObject) {
                            console.warn('[WebRTCPlayer] 视频轨道被静音，尝试重新设置流');
                            const currentStream = videoRef.current.srcObject as MediaStream;
                            // 获取所有轨道
                            const allTracks = [
                                ...currentStream.getVideoTracks(),
                                ...currentStream.getAudioTracks()
                            ];
                            // 创建新流
                            const newStream = new MediaStream(allTracks);
                            videoRef.current.srcObject = newStream;
                            
                            // 延迟尝试播放
                            setTimeout(() => {
                                if (videoRef.current && autoPlay && videoRef.current.paused) {
                                videoRef.current.play().catch((e: any) => {
                                        if (e.name !== 'AbortError') {
                                            console.error('[WebRTCPlayer] 播放失败:', e);
                                    }
                                });
                            }
                            }, 200);
                        }
                    }
                    
                    // 确保轨道已启用
                        if (!track.enabled) {
                        console.log(`[WebRTCPlayer] 启用 ${track.kind} 轨道`);
                            track.enabled = true;
                    }
                };
                pc.addEventListener('track', handleTrack);
                
                // 在清理时移除监听器
                const originalCleanup = () => {
                    pc.removeEventListener('track', handleTrack);
                    if (playerRef.current && handleRemoteStream) {
                        const eventName = 'WEBRTC_ON_REMOTE_STREAMS';
                        if ((playerRef.current as any).off) {
                            (playerRef.current as any).off(eventName, handleRemoteStream);
                        }
                    }
                };
                // 保存原始清理函数，在useEffect cleanup中调用
                (playerRef.current as any)._cleanupBufferConfig = originalCleanup;
            }

            // 监听视频事件以更新加载状态
            const videoEl = videoRef.current;
            
            // 监听 loadedmetadata 事件，确保视频元数据已加载
            const handleLoadedMetadata = () => {
                console.log('[WebRTCPlayer] Video metadata loaded');
                if (videoEl) {
                    console.log('[WebRTCPlayer] Video metadata:', {
                        videoWidth: videoEl.videoWidth,
                        videoHeight: videoEl.videoHeight,
                        readyState: videoEl.readyState,
                        hasSrcObject: !!videoEl.srcObject
                    });
                    
                    // 如果元数据已加载但还没有播放，尝试播放
                    if (autoPlay && videoEl.paused) {
                        console.log('[WebRTCPlayer] Metadata loaded, attempting to play...');
                        videoEl.play().catch((e) => {
                            if (e.name !== 'AbortError') {
                                console.error('[WebRTCPlayer] Failed to play after metadata loaded:', e);
                            }
                        });
                    }
                }
            };
            
            const handlePlaying = () => {
                console.log('[WebRTCPlayer] Video playing event triggered');
                setLoading(false);
                setIsPlaying(true);
                
                // 确保音频未静音
                if (videoEl && videoEl.muted && !muted) {
                    console.log('[WebRTCPlayer] Video was muted, unmuting...');
                    videoEl.muted = false;
                }
                
                // 强制播放，确保视频真正开始播放
                if (videoEl && videoEl.paused) {
                    console.log('[WebRTCPlayer] Video is paused, attempting to play...');
                    videoEl.play().catch((e) => {
                        // AbortError 是正常的，当 play() 被 pause() 中断时会发生
                        if (e.name !== 'AbortError') {
                            console.error('[WebRTCPlayer] Failed to play video:', e);
                        }
                    });
                }
                
                // 检查音频轨道（延迟检查，等待轨道加载）
                setTimeout(() => {
                    if (videoEl && videoEl.srcObject) {
                        const stream = videoEl.srcObject as MediaStream;
                        const audioTracks = stream.getAudioTracks();
                        console.log(`[WebRTCPlayer] Audio tracks in stream: ${audioTracks.length}`);
                        
                        // 也检查 PeerConnection 的接收器
                        if (playerRef.current?.pc) {
                            const receivers = playerRef.current.pc.getReceivers();
                            const audioReceivers = receivers.filter((r: RTCRtpReceiver) => 
                                r.track && r.track.kind === 'audio'
                            );
                            console.log(`[WebRTCPlayer] Audio receivers: ${audioReceivers.length}`);
                            
                            audioReceivers.forEach((receiver: RTCRtpReceiver, index: number) => {
                                const track = receiver.track;
                                if (track) {
                                    console.log(`[WebRTCPlayer] Audio receiver ${index}:`, {
                                        id: track.id,
                                        enabled: track.enabled,
                                        muted: track.muted,
                                        readyState: track.readyState,
                                    });
                                    
                                    // 如果轨道未启用，启用它
                                    if (!track.enabled) {
                                        track.enabled = true;
                                        console.log(`[WebRTCPlayer] Enabled audio receiver ${index}`);
                                    }
                                    
                                    // 如果轨道被静音，取消静音
                                    if (track.muted) {
                                        console.log(`[WebRTCPlayer] Audio track is muted, trying to unmute...`);
                                        // 注意：track.muted 是只读的，需要通过其他方式处理
                                        // 尝试重新设置轨道
                                        try {
                                            // 通过设置 enabled 来尝试取消静音
                                            track.enabled = false;
                                            setTimeout(() => {
                                                track.enabled = true;
                                                console.log(`[WebRTCPlayer] Re-enabled audio track to unmute`);
                                            }, 100);
                                        } catch (e) {
                                            console.warn(`[WebRTCPlayer] Failed to unmute track:`, e);
                                        }
                                    }
                                    
                                    // 确保轨道在流中
                                    if (audioTracks.length === 0 && stream) {
                                        console.log(`[WebRTCPlayer] 添加音频轨道到流`);
                                        stream.addTrack(track);
                                    }
                                }
                            });
                        }
                        
                        audioTracks.forEach((track, index) => {
                            console.log(`[WebRTCPlayer] Audio track ${index}:`, {
                                id: track.id,
                                enabled: track.enabled,
                                muted: track.muted,
                                readyState: track.readyState,
                            });
                            if (!track.enabled) {
                                track.enabled = true;
                                console.log(`[WebRTCPlayer] Enabled audio track ${index}`);
                            }
                        });
                    }
                }, 1000); // 延迟1秒检查，等待轨道加载
            };
            const handlePause = () => {
                setIsPlaying(false);
            };
            const handleError = (e: Event) => {
                console.error('Video error:', e);
                const target = e.target as HTMLVideoElement;
                if (target && target.error) {
                    const error = target.error;
                    if (error && error.code === error.MEDIA_ERR_SRC_NOT_SUPPORTED) {
                        setError('流不存在或已停止播放。请检查流状态。');
                    } else {
                        setError('WebRTC 播放出错');
                    }
                } else {
                    setError('WebRTC 播放出错');
                }
                setLoading(false);
            };

            // 监听 WebRTC 连接错误
            if (playerRef.current) {
                const handleRTCError = (error: any) => {
                    console.error('WebRTC error:', error);
                    setError('WebRTC 连接失败。流可能不存在或已停止。');
                    setLoading(false);
                };

                // 尝试监听 WebRTC 错误事件
                if (playerRef.current.onError) {
                    playerRef.current.onError = handleRTCError;
                }
            }

            videoEl.addEventListener('loadedmetadata', handleLoadedMetadata);
            videoEl.addEventListener('playing', handlePlaying);
            videoEl.addEventListener('pause', handlePause);
            videoEl.addEventListener('error', handleError);

            // 监控播放状态
            const statsInterval = setInterval(async () => {
                if (videoRef.current && playerRef.current?.pc) {
                    const v = videoRef.current;
                    try {
                        const stats = await playerRef.current.pc.getStats();
                        let packetsReceived = 0;
                        let packetsLost = 0;
                        let framesDecoded = 0;
                        let framesDropped = 0;

                        let audioPacketsReceived = 0;
                        let audioPacketsLost = 0;
                        
                        stats.forEach((report: any) => {
                            if (report.type === 'inbound-rtp') {
                                if (report.kind === 'video') {
                                    packetsReceived = report.packetsReceived;
                                    packetsLost = report.packetsLost;
                                    framesDecoded = report.framesDecoded;
                                    framesDropped = report.framesDropped;
                                } else if (report.kind === 'audio') {
                                    audioPacketsReceived = report.packetsReceived || 0;
                                    audioPacketsLost = report.packetsLost || 0;
                                }
                            }
                        });
                        
                        // 如果检测到音频丢包，记录警告
                        if (audioPacketsLost > 0) {
                            console.warn(`[WebRTCStats] Audio packets lost: ${audioPacketsLost}`);
                        }

                    } catch (e) {
                        console.error('Failed to get stats:', e);
                    }
                }
            }, 1000);

            return () => {
                console.log('Destroying WebRTC player');
                clearInterval(statsInterval);
                if (playerRef.current) {
                    // 清理缓冲区配置监听器
                    if ((playerRef.current as any)?._cleanupBufferConfig) {
                        (playerRef.current as any)._cleanupBufferConfig();
                    }
                    playerRef.current.close();
                    playerRef.current = null;
                }
                videoEl.removeEventListener('loadedmetadata', handleLoadedMetadata);
                videoEl.removeEventListener('playing', handlePlaying);
                videoEl.removeEventListener('pause', handlePause);
                videoEl.removeEventListener('error', handleError);
            };
        } catch (err) {
            console.error('Failed to initialize WebRTC player:', err);
            setError(err instanceof Error ? err.message : '初始化播放器失败');
            setLoading(false);
        }
    }, [url]);

    const handleTogglePlay = () => {
        if (videoRef.current) {
            if (isPlaying) {
                videoRef.current.pause();
            } else {
                videoRef.current.play().then(() => {
                    setNeedsUserInteraction(false);
                    setIsPlaying(true);
                }).catch((e) => {
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
            className={`relative bg-black ${className}`}
            style={{ minHeight: '200px', width: '100%', height: '100%' }}
            onMouseEnter={() => controls && setShowControls(true)}
            onMouseLeave={() => setShowControls(false)}
        >
            {loading && !error && (
                <div className="absolute inset-0 flex items-center justify-center z-10 bg-black/50">
                    <Spin>
                        <div style={{ color: 'white', marginTop: 8 }}>正在连接 WebRTC...</div>
                    </Spin>
                </div>
            )}

            {error && (
                <div className="absolute inset-0 flex items-center justify-center z-10 bg-black/80 p-4">
                    <Alert message="播放错误" description={error} type="error" showIcon />
                </div>
            )}

            {/* 需要用户交互时显示播放按钮 */}
            {needsUserInteraction && !error && !loading && (
                <div className="absolute inset-0 flex items-center justify-center z-20 bg-black/50">
                    <div className="text-center">
                        <Button
                            type="primary"
                            size="large"
                            icon={<PlayCircleOutlined />}
                            onClick={handleTogglePlay}
                            style={{
                                fontSize: '48px',
                                width: '80px',
                                height: '80px',
                                borderRadius: '50%',
                                display: 'flex',
                                alignItems: 'center',
                                justifyContent: 'center',
                            }}
                        />
                        <div style={{ color: 'white', marginTop: 16, fontSize: '14px' }}>
                            点击播放视频
                        </div>
                    </div>
                </div>
            )}

            <video
                ref={videoRef}
                className="w-full h-full object-contain"
                autoPlay={autoPlay}
                muted={muted}
                playsInline
                onClick={handleTogglePlay}
                style={{ width: '100%', height: '100%', objectFit: 'contain' }}
                // 优化视频缓冲区：减少延迟，提高流畅性
                preload="auto"
                // 通过CSS优化渲染性能
                onLoadedMetadata={(e) => {
                    const video = e.target as HTMLVideoElement;
                    console.log('[WebRTCPlayer] Video metadata loaded:', {
                        videoWidth: video.videoWidth,
                        videoHeight: video.videoHeight,
                        duration: video.duration,
                        readyState: video.readyState,
                        paused: video.paused,
                        muted: video.muted,
                        srcObject: !!video.srcObject
                    });
                    // 设置播放速率，避免缓冲区累积
                    if (video.playbackRate !== 1.0) {
                        video.playbackRate = 1.0;
                    }
                    // 确保视频开始播放
                    if (autoPlay && video.paused) {
                        console.log('[WebRTCPlayer] Auto-playing video after metadata loaded');
                        video.play().catch((e: any) => {
                            // AbortError 是正常的，当 play() 被 pause() 中断时会发生
                            if (e.name === 'AbortError') {
                                return;
                            }
                            console.error('[WebRTCPlayer] Auto-play failed:', e);
                            // 如果是用户交互错误，显示播放按钮
                            if (e.name === 'NotAllowedError' || e.message?.includes('user didn\'t interact')) {
                                console.log('[WebRTCPlayer] 需要用户交互才能播放');
                                setNeedsUserInteraction(true);
                            }
                        });
                    }
                }}
                onCanPlay={(e) => {
                    const video = e.target as HTMLVideoElement;
                    console.log('[WebRTCPlayer] Video can play:', {
                        readyState: video.readyState,
                        paused: video.paused,
                        currentTime: video.currentTime
                    });
                    // 如果视频可以播放但处于暂停状态，尝试播放
                    if (autoPlay && video.paused && video.readyState >= 3) {
                        console.log('[WebRTCPlayer] Attempting to play video on canPlay event');
                        video.play().catch((e: any) => {
                            // AbortError 是正常的，当 play() 被 pause() 中断时会发生
                            if (e.name === 'AbortError') {
                                return;
                            }
                            console.error('[WebRTCPlayer] Play failed on canPlay:', e);
                            // 如果是用户交互错误，显示播放按钮
                            if (e.name === 'NotAllowedError' || e.message?.includes('user didn\'t interact')) {
                                console.log('[WebRTCPlayer] 需要用户交互才能播放');
                                setNeedsUserInteraction(true);
                            }
                        });
                    }
                }}
            />
            {/* 自定义控制栏：悬停时显示播放/暂停按钮 */}
            {showControls && (
                <div
                    className="absolute bottom-2 left-1/2 transform -translate-x-1/2 z-20"
                    style={{ bottom: 10, transition: 'opacity 0.3s ease' }}
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
