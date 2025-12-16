
// utils/streamIndexer.ts

export interface Stream {
    app: string;
    stream: string;
    protocol?: string;
    gateway_type?: string;
    source_url?: string;
    output_protocol?: string;
    status: number;
    [key: string]: any;
}

export class StreamIndexer {
    private baseNameIndex = new Map<string, string>();
    private fullNameIndex = new Map<string, string>();

    indexStream(stream: Stream) {
        const fullKey = `${stream.app}/${stream.stream}`;

        // Always index full name
        this.fullNameIndex.set(fullKey, fullKey);
        // Backward mapping if needed, or just set existence

        if (stream.app === 'gb28181') {
            const baseName = this.extractBaseName(stream.stream);
            const baseKey = `${stream.app}/${baseName}`;

            this.baseNameIndex.set(baseKey, fullKey);
        }
    }

    findMatch(app: string, stream: string): string | null {
        const key = `${app}/${stream}`;

        if (this.fullNameIndex.has(key)) return key;
        if (this.baseNameIndex.has(key)) return this.baseNameIndex.get(key)!;

        if (app === 'gb28181') {
            const baseName = this.extractBaseName(stream);
            const baseKey = `${app}/${baseName}`;
            // Check if we have a match for this base key
            return this.baseNameIndex.get(baseKey) || null;
        }

        return null;
    }

    clear() {
        this.baseNameIndex.clear();
        this.fullNameIndex.clear();
    }

    private extractBaseName(stream: string): string {
        // GB28181 format: deviceId_channelId_timestamp
        // E.g., 34020000001110000001_34020000001320000001_1701234567890
        // Base: 34020000001110000001_34020000001320000001
        const match = stream.match(/^(.+)_(\d{10,})$/);
        return match ? match[1] : stream;
    }
}
