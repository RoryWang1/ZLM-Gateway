// API 客户端基础类

import axios, { AxiosInstance, AxiosError, AxiosRequestConfig } from 'axios';
import { message } from 'antd';
import { API_BASE_URL } from '@/utils/constants';
import type { ApiResponse } from '@/types';

interface CustomAxiosInstance extends Omit<AxiosInstance, 'get' | 'post' | 'put' | 'delete' | 'patch'> {
  get<T = any>(url: string, config?: AxiosRequestConfig): Promise<T>;
  post<T = any>(url: string, data?: any, config?: AxiosRequestConfig): Promise<T>;
  put<T = any>(url: string, data?: any, config?: AxiosRequestConfig): Promise<T>;
  delete<T = any>(url: string, config?: AxiosRequestConfig): Promise<T>;
  patch<T = any>(url: string, data?: any, config?: AxiosRequestConfig): Promise<T>;
}

export const apiClient = axios.create({
  baseURL: API_BASE_URL,
  timeout: 10000,  // 默认10秒超时，对于设备列表等简单查询足够
  headers: {
    'Content-Type': 'application/json',
  },
}) as unknown as CustomAxiosInstance;

// 为设备发现等耗时操作创建专用客户端
export const discoveryClient = axios.create({
  baseURL: API_BASE_URL,
  timeout: 60000,  // 60秒超时，用于设备发现（可能需要扫描IP范围）
  headers: {
    'Content-Type': 'application/json',
  },
}) as unknown as CustomAxiosInstance;

// 为发现客户端添加相同的拦截器
discoveryClient.interceptors.request.use(
  (config) => {
    return config;
  },
  (error) => {
    return Promise.reject(error);
  }
);

discoveryClient.interceptors.response.use(
  (response) => {
    const data: ApiResponse = response.data;
    if (data.code !== 0) {
      message.error(data.msg || '请求失败');
      return Promise.reject(new Error(data.msg || '请求失败'));
    }
    return data.data;
  },
  (error: AxiosError) => {
    // 复用相同的错误处理逻辑
    if (error.response) {
      const status = error.response.status;
      switch (status) {
        case 400:
          message.error('请求参数错误');
          break;
        case 401:
          message.error('未授权，请重新登录');
          break;
        case 403:
          message.error('拒绝访问');
          break;
        case 404:
          break;
        case 500:
          message.error('服务器内部错误');
          break;
        default:
          message.error(`请求失败: ${status}`);
      }
    } else if (error.request) {
      if (error.code === 'ECONNABORTED' || error.message?.includes('timeout')) {
        const url = error.config?.url || 'unknown';
        console.warn('API 请求超时:', url);
        const timeoutKey = `api_timeout_${url}`;
        const lastShown = sessionStorage.getItem(timeoutKey);
        const now = Date.now();
        if (!lastShown || now - parseInt(lastShown) > 10000) {
          message.warning('API 请求超时，请检查 Gateway 服务状态', 3);
          sessionStorage.setItem(timeoutKey, now.toString());
        }
      } else if (error.code === 'ERR_NETWORK' || error.message?.includes('Network Error')) {
        const networkErrorKey = 'network_error_shown';
        if (!sessionStorage.getItem(networkErrorKey)) {
          message.error('网络连接失败，请检查 Gateway 服务是否运行', 5);
          sessionStorage.setItem(networkErrorKey, 'true');
          setTimeout(() => {
            sessionStorage.removeItem(networkErrorKey);
          }, 30000);
        }
      } else {
        console.warn('网络错误:', error.message);
      }
    } else {
      message.error('请求配置错误');
      console.error('请求配置错误:', error.message);
    }
    return Promise.reject(error);
  }
);

// 请求拦截器
apiClient.interceptors.request.use(
  (config) => {
    // 可以在这里添加认证 token 等
    return config;
  },
  (error) => {
    return Promise.reject(error);
  }
);

// 响应拦截器
apiClient.interceptors.response.use(
  (response) => {
    const data: ApiResponse = response.data;

    // 检查业务状态码
    if (data.code !== 0) {
      message.error(data.msg || '请求失败');
      return Promise.reject(new Error(data.msg || '请求失败'));
    }

    return data.data;
  },
  (error: AxiosError) => {
    // 统一错误处理
    if (error.response) {
      const status = error.response.status;
      switch (status) {
        case 400:
          message.error('请求参数错误');
          break;
        case 401:
          message.error('未授权，请重新登录');
          break;
        case 403:
          message.error('拒绝访问');
          break;
        case 404:
          // 404 错误不显示提示（可能是正常的资源不存在）
          break;
        case 500:
          message.error('服务器内部错误');
          break;
        default:
          message.error(`请求失败: ${status}`);
      }
    } else if (error.request) {
      // 网络错误：检查是否是超时
      if (error.code === 'ECONNABORTED' || error.message?.includes('timeout')) {
        // 超时错误：只在控制台记录，不频繁显示提示
        const url = error.config?.url || 'unknown';
        console.warn('API 请求超时:', url);
        // 使用 sessionStorage 避免频繁显示提示
        const timeoutKey = `api_timeout_${url}`;
        const lastShown = sessionStorage.getItem(timeoutKey);
        const now = Date.now();

        if (!lastShown || now - parseInt(lastShown) > 10000) {
          // 10秒内只显示一次超时提示
          message.warning('API 请求超时，请检查 Gateway 服务状态', 3);
          sessionStorage.setItem(timeoutKey, now.toString());
        }
      } else if (error.code === 'ERR_NETWORK' || error.message?.includes('Network Error')) {
        // 网络连接错误：只在第一次显示
        const networkErrorKey = 'network_error_shown';
        if (!sessionStorage.getItem(networkErrorKey)) {
          message.error('网络连接失败，请检查 Gateway 服务是否运行', 5);
          sessionStorage.setItem(networkErrorKey, 'true');
          // 30秒后重置
          setTimeout(() => {
            sessionStorage.removeItem(networkErrorKey);
          }, 30000);
        }
      } else {
        // 其他网络错误：静默处理，避免频繁提示
        console.warn('网络错误:', error.message);
      }
    } else {
      // 配置错误
      message.error('请求配置错误');
      console.error('请求配置错误:', error.message);
    }

    return Promise.reject(error);
  }
);

