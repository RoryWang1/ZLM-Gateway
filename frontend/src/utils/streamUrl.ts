// 流 URL 生成工具

import { ZLM_BASE_URL, ZLM_SECRET } from './constants';

export type PlayProtocol = 'http-flv' | 'hls' | 'webrtc';

/**
 * 生成流播放 URL
 */
export function getStreamPlayURL(
  app: string,
  stream: string,
  protocol: PlayProtocol
): string {
  switch (protocol) {
    case 'http-flv':
      // ZLMediaKit HTTP-FLV URL格式: /{app}/{stream}.live.flv
      // 根据ZLMediaKit文档，HTTP-FLV播放地址应该使用 .live.flv 后缀
      return `${ZLM_BASE_URL}/${app}/${stream}.live.flv`;
    case 'hls':
      return `${ZLM_BASE_URL}/${app}/${stream}/hls.m3u8`;
    case 'webrtc':
      return `${ZLM_BASE_URL}/index/api/webrtc?app=${app}&stream=${stream}&type=play`;
    default:
      return '';
  }
}

/**
 * 生成流快照 URL
 * ZLM快照API: /index/api/getSnap?secret=xxx&url=xxx&timeout_sec=5&expire_sec=10
 * 使用HTTP-FLV流的URL来生成快照
 */
export function getStreamSnapshotURL(
  app: string,
  stream: string,
  outputProtocol?: string
): string {
  // 在开发环境中，使用代理路径避免CORS问题
  const isDev = import.meta.env.DEV;
  const baseUrl = isDev ? '' : ZLM_BASE_URL; // 开发环境使用代理，生产环境使用完整URL
  
  // 使用HTTP-FLV URL来获取快照（根据ZLM文档和测试脚本，这是推荐的方式）
  // HTTP-FLV URL格式: http://localhost:8081/{app}/{stream}.live.flv
  // 注意：ZLM文档和测试脚本都使用HTTP-FLV URL，而不是RTMP URL
  const streamUrl = `${ZLM_BASE_URL}/${app}/${stream}.live.flv`;
  
  const params = new URLSearchParams();
  
  // Secret是必需的，如果没有配置，快照API会失败
  if (!ZLM_SECRET) {
    console.error(`[getStreamSnapshotURL] ❌ ZLM_SECRET未配置！快照功能无法使用。请检查环境变量 VITE_ZLM_SECRET`);
    // 即使没有secret，也返回URL，让浏览器显示错误
  } else {
    params.append('secret', ZLM_SECRET);
  }
  
  params.append('url', streamUrl);
  params.append('timeout_sec', '20');  // 增加到20秒，给快照API更多时间等待I帧（关键帧）
  params.append('expire_sec', '10');  // 快照有效期10秒
  
  const snapshotUrl = `${baseUrl}/index/api/getSnap?${params.toString()}`;
  
  // 开发环境调试输出
  if (isDev) {
    console.log(`[getStreamSnapshotURL] 生成快照URL:`, {
      app,
      stream,
      outputProtocol: outputProtocol || '未指定',
      streamUrl,
      urlType: 'HTTP-FLV',
      hasSecret: !!ZLM_SECRET,
      ZLM_BASE_URL,
      ZLM_SECRET: ZLM_SECRET ? `${ZLM_SECRET.substring(0, 4)}...` : '未设置',
      snapshotUrl,
      baseUrl: baseUrl || '(使用代理)',
      note: '使用HTTP-FLV URL获取快照（根据ZLM文档和测试脚本，这是推荐的方式）',
    });
  }
  
  return snapshotUrl;
}

/**
 * 获取ZLM默认快照（logo）URL
 * 当流快照不可用时，显示ZLM的默认logo
 * ZLM配置中的defaultSnap路径是 ./www/logo.png
 */
export function getDefaultSnapshotURL(): string {
  const isDev = import.meta.env.DEV;
  const baseUrl = isDev ? '' : ZLM_BASE_URL;
  // ZLM的logo路径，根据配置 defaultSnap=./www/logo.png
  return `${baseUrl}/logo.png`;
}

