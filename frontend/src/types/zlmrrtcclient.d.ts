interface ZLMRTCClientEndpoint {
    close(): void;
}

interface ZLMRTCClientOptions {
    element: HTMLVideoElement;
    debug?: boolean;
    zlmsdpUrl: string;
    simulcast?: boolean;
    useCamera?: boolean;
    audioEnable?: boolean;
    videoEnable?: boolean;
    recvOnly?: boolean;
    resolution?: { w: number; h: number };
    forceTcp?: boolean;
}

declare class ZLMRTCClient {
    static Endpoint: {
        new(options: ZLMRTCClientOptions): ZLMRTCClientEndpoint;
    };
}

interface Window {
    ZLMRTCClient: typeof ZLMRTCClient;
}
