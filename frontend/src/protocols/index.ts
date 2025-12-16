// 协议模块入口 - 注册所有协议适配器

import { protocolRegistry } from './registry';
import { ONVIFAdapter } from './adapters/ONVIFAdapter';
import { ISAPIAdapter } from './adapters/ISAPIAdapter';
import { DahuaAdapter } from './adapters/DahuaAdapter';
import { PSIAAdapter } from './adapters/PSIAAdapter';
import { LocalCameraAdapter } from './adapters/LocalCameraAdapter';
import { GB28181Adapter } from './adapters/GB28181Adapter';

// 注册所有协议适配器
protocolRegistry.register(new ONVIFAdapter());
protocolRegistry.register(new ISAPIAdapter());
protocolRegistry.register(new DahuaAdapter());
protocolRegistry.register(new PSIAAdapter());
protocolRegistry.register(new LocalCameraAdapter());
protocolRegistry.register(new GB28181Adapter());

// 导出
export { protocolRegistry } from './registry';
export type { IProtocolAdapter, DiscoveryOptions, ProtocolConfig, ProtocolFormField } from './types';
export { ONVIFAdapter } from './adapters/ONVIFAdapter';
export { ISAPIAdapter } from './adapters/ISAPIAdapter';
export { DahuaAdapter } from './adapters/DahuaAdapter';
export { PSIAAdapter } from './adapters/PSIAAdapter';
export { LocalCameraAdapter } from './adapters/LocalCameraAdapter';
export { GB28181Adapter } from './adapters/GB28181Adapter';

