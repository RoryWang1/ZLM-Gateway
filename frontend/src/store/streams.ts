// 流状态管理

import { create } from 'zustand';
import type { Stream } from '@/types/stream';

interface StreamsState {
  streams: Stream[];
  loading: boolean;
  error: string | null;
  setStreams: (streams: Stream[]) => void;
  addStream: (stream: Stream) => void;
  updateStream: (app: string, stream: string, updates: Partial<Stream>) => void;
  removeStream: (app: string, stream: string) => void;
  setLoading: (loading: boolean) => void;
  setError: (error: string | null) => void;
}

export const useStreamsStore = create<StreamsState>((set) => ({
  streams: [],
  loading: false,
  error: null,
  setStreams: (streams) => set({ streams }),
  addStream: (stream) =>
    set((state) => ({
      streams: [...state.streams, stream],
    })),
  updateStream: (app, stream, updates) =>
    set((state) => ({
      streams: state.streams.map((s) =>
        s.app === app && s.stream === stream ? { ...s, ...updates } : s
      ),
    })),
  removeStream: (app, stream) =>
    set((state) => ({
      streams: state.streams.filter((s) => !(s.app === app && s.stream === stream)),
    })),
  setLoading: (loading) => set({ loading }),
  setError: (error) => set({ error }),
}));

