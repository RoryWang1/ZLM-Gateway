// 设备状态管理

import { create } from 'zustand';
import type { Device } from '@/types/device';

interface DevicesState {
  devices: Device[];
  loading: boolean;
  error: string | null;
  setDevices: (devices: Device[]) => void;
  addDevice: (device: Device) => void;
  updateDevice: (deviceId: string, updates: Partial<Device>) => void;
  removeDevice: (deviceId: string) => void;
  setLoading: (loading: boolean) => void;
  setError: (error: string | null) => void;
}

export const useDevicesStore = create<DevicesState>((set) => ({
  devices: [],
  loading: false,
  error: null,
  setDevices: (devices) => set({ devices }),
  addDevice: (device) =>
    set((state) => ({
      devices: [...state.devices, device],
    })),
  updateDevice: (deviceId, updates) =>
    set((state) => ({
      devices: state.devices.map((d) =>
        d.device_id === deviceId ? { ...d, ...updates } : d
      ),
    })),
  removeDevice: (deviceId) =>
    set((state) => ({
      devices: state.devices.filter((d) => d.device_id !== deviceId),
    })),
  setLoading: (loading) => set({ loading }),
  setError: (error) => set({ error }),
}));

