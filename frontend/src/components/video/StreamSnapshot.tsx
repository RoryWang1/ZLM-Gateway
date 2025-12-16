import React, { useState, useEffect } from 'react';
import { Spin } from 'antd';
import { getStreamSnapshotURL, getDefaultSnapshotURL } from '@/utils/streamUrl';

export interface StreamSnapshotProps {
    app: string;
    stream: string;
    streamKey: string;
    isStreamAvailable: boolean;
    status?: number | string;
    outputProtocol?: string; // 输出协议，用于选择正确的快照URL
    zlmAlive?: boolean; // ZLM中流是否活跃
    uptimeSeconds?: number; // 流的运行时间（秒），用于判断是否需要等待
}

export default function StreamSnapshot({ app, stream, streamKey, isStreamAvailable, status, outputProtocol, zlmAlive, uptimeSeconds }: StreamSnapshotProps) {
    const [imageError, setImageError] = useState(false);
    const [isDefaultLogo, setIsDefaultLogo] = useState(false); // 记录是否是默认logo
    // 使用sessionStorage保存快照URL，避免组件重新挂载时丢失
    const snapshotStorageKey = `snapshot_${streamKey}`;
    const getStoredSnapshotUrl = () => {
        try {
            return sessionStorage.getItem(snapshotStorageKey);
        } catch {
            return null;
        }
    };
    const setStoredSnapshotUrl = (url: string | null) => {
        try {
            if (url) {
                sessionStorage.setItem(snapshotStorageKey, url);
            } else {
                sessionStorage.removeItem(snapshotStorageKey);
            }
        } catch {
            // 忽略存储错误
        }
    };

    // 从sessionStorage恢复快照URL（在useState初始化时就恢复，避免黑屏）
    const storedSnapshotUrl = getStoredSnapshotUrl();
    const [currentImageUrl, setCurrentImageUrl] = useState<string | null>(storedSnapshotUrl);
    const [refreshKey, setRefreshKey] = useState(0); // 用于强制刷新图片
    // 如果有旧快照，不需要初始延迟（直接显示快照）
    const [initialDelay, setInitialDelay] = useState(!storedSnapshotUrl); // 如果有旧快照，跳过初始延迟
    // 如果有旧快照，立即设置状态（避免显示loading）
    const [imageLoading, setImageLoading] = useState(!storedSnapshotUrl); // 如果有旧快照，不显示loading
    const [hasLoaded, setHasLoaded] = useState(!!storedSnapshotUrl); // 如果有旧快照，标记为已加载
    const snapshotUrl = getStreamSnapshotURL(app, stream, outputProtocol);
    const defaultLogoUrl = getDefaultSnapshotURL();

    // 当快照URL变化时，重置状态（但保留当前显示的图片）
    // 同时监听isStreamAvailable，如果流不可用，重置状态
    useEffect(() => {
        // 只有在URL真正变化时才重置（避免不必要的重置）
        // 注意：不重置currentImageUrl，保留上一个快照
        // 只有在没有旧快照时才设置imageLoading为true
        const storedUrl = getStoredSnapshotUrl();
        const hasOldImage = currentImageUrl || storedUrl;
        if (!hasOldImage) {
            setImageLoading(true);
            setInitialDelay(true); // 只有在没有旧快照时才重置初始延迟
        } else {
            // 如果有旧快照，确保状态正确
            if (!currentImageUrl && storedUrl) {
                setCurrentImageUrl(storedUrl);
                setImageLoading(false);
                setHasLoaded(true);
            }
            // 如果有旧快照，不重置初始延迟（保持显示快照）
        }
        setImageError(false);
        setIsDefaultLogo(false);
    }, [snapshotUrl, isStreamAvailable]); // 添加isStreamAvailable依赖，当流状态变化时重置

    // 定期更新快照（每30秒刷新一次）
    useEffect(() => {
        if (!isStreamAvailable || initialDelay) {
            return; // 如果流不可用或还在初始延迟中，不启动定期刷新
        }

        // 如果流已经运行了超过30秒，立即开始定期刷新
        // 如果流刚启动，等待15秒后再开始定期刷新
        const startRefreshDelay = uptimeSeconds !== undefined && uptimeSeconds > 30 ? 0 : 15000;

        let refreshInterval: ReturnType<typeof setInterval> | null = null;

        const startTimer = setTimeout(() => {
            if (import.meta.env.DEV) {
                console.log(`[StreamSnapshot] ${streamKey} - 开始定期刷新快照（每30秒）`);
            }

            // 立即触发第一次刷新
            setRefreshKey(prev => prev + 1);

            // 每30秒刷新一次快照
            refreshInterval = setInterval(() => {
                setRefreshKey(prev => {
                    const newKey = prev + 1;
                    if (import.meta.env.DEV) {
                        console.log(`[StreamSnapshot] ${streamKey} - 定期刷新快照 (refreshKey: ${newKey})`);
                    }
                    return newKey;
                });
            }, 30000); // 30秒刷新一次
        }, startRefreshDelay);

        return () => {
            clearTimeout(startTimer);
            if (refreshInterval) {
                clearInterval(refreshInterval);
            }
        };
    }, [isStreamAvailable, initialDelay, streamKey, uptimeSeconds]); // 移除currentImageUrl和refreshKey依赖，避免不断重新创建定时器

    // 初始延迟：等待流完全启动后再获取快照
    // 优化：根据流的运行时间判断是否需要等待
    // - 如果有旧快照，不需要等待（直接显示旧快照）
    // - 如果流已经运行了超过30秒，不需要等待（流已经稳定）
    // - 如果流刚启动（运行时间少于30秒），需要等待15秒（等待I帧）
    useEffect(() => {
        // 如果有旧快照，跳过初始延迟
        if (currentImageUrl || getStoredSnapshotUrl()) {
            if (initialDelay) {
                setInitialDelay(false);
                if (import.meta.env.DEV) {
                    console.log(`[StreamSnapshot] ${streamKey} - 有旧快照，跳过初始延迟，直接显示快照`);
                }
            }
            return;
        }

        if (initialDelay && isStreamAvailable) {
            // 如果流已经运行了超过30秒，不需要等待
            if (uptimeSeconds !== undefined && uptimeSeconds > 30) {
                setInitialDelay(false);
                if (import.meta.env.DEV) {
                    console.log(`[StreamSnapshot] ${streamKey} - 流已运行${uptimeSeconds}秒，跳过延迟，直接获取快照`, {
                        uptimeSeconds,
                        zlmAlive: zlmAlive !== undefined ? zlmAlive : '未设置',
                    });
                }
                return;
            }

            // 如果流在ZLM中不活跃，等待更长时间（30秒）
            // 如果流在ZLM中活跃，等待15秒（比ZLM的wait_track_ready_ms=10秒稍长）
            const delayTime = zlmAlive === false ? 30000 : 15000;

            const delayTimer = setTimeout(() => {
                setInitialDelay(false);
                if (import.meta.env.DEV) {
                    console.log(`[StreamSnapshot] ${streamKey} - 初始延迟完成（等待${delayTime / 1000}秒），开始获取快照`, {
                        uptimeSeconds: uptimeSeconds !== undefined ? uptimeSeconds : '未设置',
                        zlmAlive: zlmAlive !== undefined ? zlmAlive : '未设置',
                        note: zlmAlive === false ? '⚠️ 流在ZLM中不活跃，等待了更长时间' : '✅ 流在ZLM中活跃',
                    });
                }
            }, delayTime);

            return () => clearTimeout(delayTimer);
        }
    }, [initialDelay, isStreamAvailable, streamKey, zlmAlive, uptimeSeconds, currentImageUrl]);

    // 注意：不再停止自动刷新，即使返回默认logo也继续定期刷新
    // 因为用户要求定期更新快照，即使返回默认logo也应该继续尝试获取新快照

    // 调试：输出快照URL（开发环境）- 只在URL变化时输出，避免频繁日志
    useEffect(() => {
        if (import.meta.env.DEV) {
            console.log(`[StreamSnapshot] ${streamKey} - 快照URL:`, snapshotUrl);
        }
    }, [snapshotUrl, streamKey]);

    // 调试：输出当前状态（开发环境）- 只在关键状态变化时输出
    // 注意：必须在条件返回之前调用所有hooks
    useEffect(() => {
        if (import.meta.env.DEV && (imageError || hasLoaded)) {
            console.log(`[StreamSnapshot] ${streamKey} - 当前状态:`, {
                imageError,
                imageLoading,
                hasLoaded,
                hasCurrentImage: !!currentImageUrl,
                outputProtocol: outputProtocol || '未指定',
                zlmAlive: zlmAlive !== undefined ? zlmAlive : '未设置',
            });
        }
    }, [imageError, hasLoaded, streamKey, currentImageUrl, outputProtocol, zlmAlive, imageLoading]);

    // 如果流不可用，显示错误提示
    // 注意：所有hooks必须在条件返回之前调用
    if (!isStreamAvailable) {
        return (
            <div
                style={{
                    width: '100%',
                    height: '100%',
                    display: 'flex',
                    alignItems: 'center',
                    justifyContent: 'center',
                    backgroundColor: '#1f1f1f',
                    color: '#ff4d4f',
                    fontSize: '14px',
                    textAlign: 'center',
                    padding: '16px',
                }}
            >
                {status === 4 || status === 'error'
                    ? '流启动失败'
                    : '流不可用'}
            </div>
        );
    }

    // 如果还在初始延迟中，但有旧快照，直接显示快照（不显示等待提示）
    // 只有在没有旧快照时才显示等待提示
    const hasStoredSnapshot = getStoredSnapshotUrl();
    if (initialDelay && isStreamAvailable && !currentImageUrl && !hasStoredSnapshot) {
        return (
            <div
                style={{
                    width: '100%',
                    height: '100%',
                    display: 'flex',
                    flexDirection: 'column',
                    alignItems: 'center',
                    justifyContent: 'center',
                    backgroundColor: '#1f1f1f',
                    color: '#8c8c8c',
                    gap: '8px',
                }}
            >
                <Spin size="small" />
                <span style={{ fontSize: '12px' }}>等待视频轨道准备...</span>
            </div>
        );
    }

    // 如果图片加载失败且从未成功加载过，显示ZLM默认logo
    if (imageError && !imageLoading && !hasLoaded) {
        if (import.meta.env.DEV) {
            console.warn(`[StreamSnapshot] ⚠️ ${streamKey} - 显示默认logo（快照加载失败）`, {
                snapshotUrl,
                defaultLogoUrl,
                imageError,
                imageLoading,
            });
        }
        return (
            <img
                src={defaultLogoUrl}
                alt="ZLMediaKit Logo"
                style={{
                    width: '100%',
                    height: '100%',
                    objectFit: 'contain',
                    backgroundColor: '#fff',
                }}
            />
        );
    }

    // 如果快照加载成功但返回的是默认logo，显示提示信息
    // 注意：由于ZLM快照API在流没有视频帧时会返回默认logo，我们无法在浏览器中准确检测
    // 如果用户能看到视频播放，说明流有视频帧，但快照API可能无法获取到快照
    // 这种情况下，我们正常显示快照（即使是默认logo），不显示"等待视频帧"提示
    // 只在确实检测到是直接指向默认logo的URL时才显示提示
    if (hasLoaded && isDefaultLogo && !imageError) {
        // 检查是否是直接指向默认logo的URL（不是快照API返回的）
        const isDirectLogoUrl = snapshotUrl.includes('/logo.png') && !snapshotUrl.includes('/index/api/getSnap');
        if (isDirectLogoUrl) {
            // 如果是直接指向默认logo的URL，显示提示
            if (import.meta.env.DEV) {
                console.warn(`[StreamSnapshot] ⚠️ ${streamKey} - 快照返回的是默认logo，流可能没有视频帧`);
            }
            return (
                <div style={{ position: 'relative', width: '100%', height: '100%' }}>
                    <img
                        src={snapshotUrl}
                        alt={streamKey}
                        style={{
                            width: '100%',
                            height: '100%',
                            objectFit: 'cover',
                        }}
                    />
                    <div
                        style={{
                            position: 'absolute',
                            bottom: 0,
                            left: 0,
                            right: 0,
                            background: 'rgba(0, 0, 0, 0.7)',
                            color: '#fff',
                            padding: '8px',
                            fontSize: '12px',
                            textAlign: 'center',
                        }}
                    >
                        等待视频帧...
                    </div>
                </div>
            );
        }
        // 如果是快照API返回的（即使可能是默认logo），也正常显示，不显示提示
        // 因为用户能看到视频播放，说明流有视频帧，快照API可能只是暂时无法获取到快照
    }

    // 生成带时间戳的快照URL，确保定期刷新时重新加载
    // 使用Date.now()确保每次都是新的URL，避免浏览器缓存
    const snapshotUrlWithTimestamp = `${snapshotUrl}${snapshotUrl.includes('?') ? '&' : '?'}_t=${Date.now()}-${refreshKey}`;

    return (
        <>
            {/* 只在首次加载且没有旧快照时显示loading */}
            {imageLoading && !currentImageUrl && !getStoredSnapshotUrl() && !hasLoaded && (
                <div
                    style={{
                        position: 'absolute',
                        top: 0,
                        left: 0,
                        right: 0,
                        bottom: 0,
                        display: 'flex',
                        alignItems: 'center',
                        justifyContent: 'center',
                        backgroundColor: '#1f1f1f',
                        zIndex: 1,
                    }}
                >
                    <Spin size="small" />
                </div>
            )}
            {/* 显示当前快照（如果有） */}
            {currentImageUrl && (
                <img
                    key={`current-snapshot-${currentImageUrl}`}
                    src={currentImageUrl}
                    alt={streamKey}
                    style={{
                        width: '100%',
                        height: '100%',
                        objectFit: 'cover',
                        position: 'absolute',
                        top: 0,
                        left: 0,
                        zIndex: 0,
                    }}
                />
            )}
            {/* 预加载新快照（在后台加载，不显示） */}
            {/* 只有在initialDelay完成后才加载快照 */}
            {!initialDelay && (
                <img
                    key={`snapshot-preload-${refreshKey}`}
                    src={snapshotUrlWithTimestamp}
                    alt={streamKey}
                    crossOrigin="anonymous"
                    style={{
                        position: 'absolute',
                        top: '-9999px',
                        left: '-9999px',
                        width: '1px',
                        height: '1px',
                        opacity: 0,
                        pointerEvents: 'none',
                        visibility: 'hidden',
                    }}
                    onLoad={(e) => {
                        const img = e.target as HTMLImageElement;
                        // 新快照加载成功后，更新当前显示的图片
                        // 注意：保留时间戳参数，确保每次刷新时URL都不同，触发图片重新加载
                        const cleanUrl = img.src.split('&_t=')[0].split('?_t=')[0]; // 移除时间戳参数，获取基础URL
                        const hadOldImage = !!currentImageUrl;

                        // 每次刷新都更新，因为即使URL相同，图片内容可能已经变化
                        // 使用refreshKey确保每次都是新的URL
                        const newImageUrl = `${cleanUrl}${cleanUrl.includes('?') ? '&' : '?'}_v=${refreshKey}`;

                        // 同时更新sessionStorage和state，确保组件重新挂载时也能保留
                        setStoredSnapshotUrl(newImageUrl);
                        setCurrentImageUrl(newImageUrl);
                        setImageLoading(false);
                        setImageError(false); // 清除错误状态，确保显示快照而不是默认logo
                        setHasLoaded(true); // 标记已成功加载

                        if (import.meta.env.DEV) {
                            console.log(`[StreamSnapshot] ✅ ${streamKey} - 快照加载成功（${hadOldImage ? '后台更新' : '首次加载'}）`, {
                                naturalWidth: img.naturalWidth,
                                naturalHeight: img.naturalHeight,
                                refreshKey,
                                hadOldImage,
                            });
                        }

                        // 检测是否是默认logo：
                        // ZLM快照API在流没有视频帧时会返回默认logo（尺寸1024x512）
                        // 由于浏览器限制，我们无法直接比较图片内容（MD5）
                        // 但是可以通过以下方式判断：
                        // 1. 如果URL直接指向/logo.png，肯定是默认logo
                        // 2. 如果图片尺寸是1024x512，且是快照API返回的，很可能是默认logo
                        // 但是，我们不能仅凭尺寸判断，因为视频快照也可能是这个尺寸
                        // 所以这里采用保守策略：只检测直接指向默认logo的URL
                        // 对于快照API返回的图片，即使可能是默认logo，也正常显示
                        // 检测是否是默认logo：
                        // ZLM默认logo的尺寸是1024x512，这是最可靠的判断依据
                        // 1. 如果URL直接指向/logo.png，肯定是默认logo
                        // 2. 如果图片尺寸是1024x512，很可能是默认logo（即使zlmAlive=true也可能返回默认logo）
                        // 注意：虽然理论上视频快照也可能是1024x512，但实际测试发现ZLM快照API返回默认logo时总是这个尺寸
                        const isDirectLogoUrl = img.src.includes('/logo.png') && !img.src.includes('/index/api/getSnap');
                        const isDefaultSize = img.naturalWidth === 1024 && img.naturalHeight === 512;
                        // 如果尺寸是1024x512，很可能是默认logo（即使zlmAlive=true也可能返回默认logo）
                        // 因为实际测试发现，即使流活跃，快照API也可能返回默认logo
                        const isLikelyDefault = isDefaultSize;
                        const isDefault = isDirectLogoUrl || isLikelyDefault;

                        // 如果图片尺寸是1024x512，记录日志以便调试
                        if (isDefaultSize && import.meta.env.DEV) {
                            console.log(`[StreamSnapshot] ${streamKey} - 快照尺寸是1024x512`, {
                                src: img.src,
                                isDefault,
                                isDirectLogoUrl,
                                isLikelyDefault,
                                zlmAlive: zlmAlive !== undefined ? zlmAlive : '未设置',
                                note: isLikelyDefault
                                    ? '⚠️ 很可能是默认logo（尺寸1024x512，ZLM默认logo的特征尺寸）'
                                    : '正常快照'
                            });
                        }

                        setIsDefaultLogo(isDefault);
                    }}
                    onError={(e) => {
                        const img = e.target as HTMLImageElement;
                        // 如果已经有旧快照，保留旧快照，不显示错误（静默失败）
                        if (currentImageUrl) {
                            if (import.meta.env.DEV) {
                                console.warn(`[StreamSnapshot] ⚠️ ${streamKey} - 新快照加载失败，保留旧快照（后台更新失败）`, {
                                    refreshKey,
                                    snapshotUrl,
                                });
                            }
                            // 不更新状态，保留旧快照显示
                            return;
                        }

                        // 如果没有旧快照，显示错误（首次加载失败）
                        setImageLoading(false);
                        setImageError(true);
                        if (import.meta.env.DEV) {
                            console.error(`[StreamSnapshot] ❌ ${streamKey} - 快照加载失败（首次加载）`, {
                                snapshotUrl,
                                outputProtocol: outputProtocol || '未指定',
                                naturalWidth: img.naturalWidth,
                                naturalHeight: img.naturalHeight,
                                complete: img.complete,
                                currentSrc: img.currentSrc,
                            });
                            // 提供调试建议
                            console.warn(`[StreamSnapshot] 调试建议:`);
                            console.warn(`  1. 检查浏览器控制台的Network标签，查看快照请求的状态`);
                            console.warn(`  2. 尝试在浏览器中直接访问: ${snapshotUrl}`);
                            console.warn(`  3. 检查ZLM服务是否运行: http://localhost:8081`);
                            console.warn(`  4. 检查环境变量 VITE_ZLM_SECRET 是否正确配置`);
                            console.warn(`  5. 对于WebRTC流，确保ZLM中有RTSP流可用`);
                        }
                    }}
                />
            )}
        </>
    );
}
