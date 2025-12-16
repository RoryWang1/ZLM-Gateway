type StreamUpdateCallback = (data: any) => void;

class WebSocketService {
    private ws: WebSocket | null = null;
    private url: string;
    private reconnectTimer: ReturnType<typeof setTimeout> | null = null;
    private listeners: StreamUpdateCallback[] = [];
    private isConnecting: boolean = false;
    private listenerCount: number = 0; // 引用计数

    constructor() {
        // 优先使用环境变量配置的 WebSocket URL
        // 如果未配置，则根据当前环境选择：
        // - 开发环境：尝试通过 Vite 代理（如果前端服务器运行）
        // - 生产环境：直接连接到 Gateway WebSocket 服务器
        const wsUrl = import.meta.env.VITE_WS_URL;
        if (wsUrl) {
            console.log('[WebSocket] 使用环境变量 VITE_WS_URL:', wsUrl);
            this.url = wsUrl;
        } else {
            // 检查是否在开发环境（Vite 开发服务器）
            const isDev = import.meta.env.DEV;
            const currentPort = window.location.port;
            const currentHost = window.location.hostname;

            console.log('[WebSocket] 环境检测:', { isDev, currentPort, currentHost, location: window.location.href });

            // 如果当前在 Vite 开发服务器（端口 5173），使用代理
            if (isDev && (currentPort === '5173' || currentPort === '')) {
                // 开发环境：通过 Vite 代理
                const protocol = window.location.protocol === 'https:' ? 'wss:' : 'ws:';
                const host = currentHost;
                const port = currentPort || '5173';
                this.url = `${protocol}//${host}:${port}/ws/streams`;
                console.log('[WebSocket] 开发环境，使用 Vite 代理:', this.url);
            } else {
                // 生产环境或代理不可用：直接连接到 Gateway WebSocket 服务器
                // 默认端口 8092，可以通过环境变量配置
                const wsPort = import.meta.env.VITE_WS_PORT || '8092';
                const protocol = window.location.protocol === 'https:' ? 'wss:' : 'ws:';
                const host = currentHost;
                this.url = `${protocol}//${host}:${wsPort}/ws/streams`;
                console.log('[WebSocket] 生产环境，直接连接:', this.url);
            }
        }
    }

    public connect() {
        if (this.ws || this.isConnecting) return;

        this.isConnecting = true;
        console.log(`Connecting to WebSocket: ${this.url}`);

        try {
            this.ws = new WebSocket(this.url);

            this.ws.onopen = () => {
                console.log('WebSocket connected');
                this.isConnecting = false;
                this.notifyConnectionListeners();
                if (this.reconnectTimer) {
                    clearTimeout(this.reconnectTimer);
                    this.reconnectTimer = null;
                }
            };

            this.ws.onclose = (event) => {
                console.log('WebSocket disconnected', event.code, event.reason);
                this.ws = null;
                this.isConnecting = false;
                // 只有在还有监听器时才自动重连
                if (this.listenerCount > 0) {
                    this.scheduleReconnect();
                }
            };

            this.ws.onerror = (error) => {
                console.error('WebSocket error:', error);
                this.ws = null;
                this.isConnecting = false;
            };

            this.ws.onmessage = (event) => {
                try {
                    const message = JSON.parse(event.data);
                    this.notifyListeners(message);
                } catch (e) {
                    console.error('Error parsing WebSocket message:', e);
                }
            };
        } catch (e) {
            console.error('Error creating WebSocket:', e);
            this.isConnecting = false;
            if (this.listenerCount > 0) {
                this.scheduleReconnect();
            }
        }
    }

    public disconnect() {
        // 只有当没有监听器时才真正断开连接
        if (this.listenerCount === 0) {
            if (this.ws) {
                // 使用正常关闭代码，避免触发错误
                try {
                    if (this.ws.readyState === WebSocket.OPEN || this.ws.readyState === WebSocket.CONNECTING) {
                        this.ws.close(1000, 'Normal closure');
                    }
                } catch (e) {
                    // 忽略关闭错误
                }
                this.ws = null;
            }
            if (this.reconnectTimer) {
                clearTimeout(this.reconnectTimer);
                this.reconnectTimer = null;
            }
        }
    }

    public addListener(callback: StreamUpdateCallback) {
        this.listeners.push(callback);
        this.listenerCount++;
        // 有新的监听器时，确保连接已建立
        if (this.listenerCount === 1) {
            this.connect();
        }
    }

    public removeListener(callback: StreamUpdateCallback) {
        this.listeners = this.listeners.filter(l => l !== callback);
        this.listenerCount--;
        // 当没有监听器时，断开连接
        if (this.listenerCount === 0) {
            this.disconnect();
        }
    }

    private notifyListeners(data: any) {
        this.listeners.forEach(listener => listener(data));
    }

    private connectionListeners: (() => void)[] = [];

    public addConnectionListener(callback: () => void) {
        this.connectionListeners.push(callback);
    }

    public removeConnectionListener(callback: () => void) {
        this.connectionListeners = this.connectionListeners.filter(l => l !== callback);
    }

    private notifyConnectionListeners() {
        this.connectionListeners.forEach(listener => listener());
    }

    private scheduleReconnect() {
        if (this.reconnectTimer) return;

        console.log('Scheduling reconnect in 5s...');
        this.reconnectTimer = setTimeout(() => {
            this.reconnectTimer = null;
            this.connect();
        }, 5000);
    }
}

export const webSocketService = new WebSocketService();
