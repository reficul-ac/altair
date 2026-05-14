import { contextBridge, ipcRenderer } from 'electron';
import type { MavlinkServiceConfig, VehicleStatePayload } from './mavlink.js';

export type AltairVisualizerApi = {
  onVehicleState: (callback: (message: VehicleStatePayload) => void) => () => void;
  onConfig: (callback: (config: MavlinkServiceConfig) => void) => () => void;
  getConfig: () => Promise<MavlinkServiceConfig>;
  setQgcForwarding: (enabled: boolean) => Promise<MavlinkServiceConfig>;
  setListenPort: (port: number) => Promise<MavlinkServiceConfig>;
};

const api: AltairVisualizerApi = {
  onVehicleState(callback) {
    const listener = (_event: Electron.IpcRendererEvent, message: VehicleStatePayload): void => callback(message);
    ipcRenderer.on('vehicle-state', listener);
    return () => ipcRenderer.off('vehicle-state', listener);
  },
  onConfig(callback) {
    const listener = (_event: Electron.IpcRendererEvent, config: MavlinkServiceConfig): void => callback(config);
    ipcRenderer.on('mavlink-config', listener);
    return () => ipcRenderer.off('mavlink-config', listener);
  },
  getConfig() {
    return ipcRenderer.invoke('mavlink:get-config');
  },
  setQgcForwarding(enabled) {
    return ipcRenderer.invoke('mavlink:set-qgc-forwarding', enabled);
  },
  setListenPort(port) {
    return ipcRenderer.invoke('mavlink:set-listen-port', port);
  }
};

contextBridge.exposeInMainWorld('altairVisualizer', api);
