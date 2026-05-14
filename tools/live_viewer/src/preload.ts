import { contextBridge, ipcRenderer } from 'electron';
import type { MavlinkServiceConfig, SessionSnapshotPayload, VehicleStatePayload } from './mavlink.js';

export type AltairVisualizerApi = {
  onVehicleState: (callback: (message: VehicleStatePayload) => void) => () => void;
  onSessionSnapshot: (callback: (message: SessionSnapshotPayload) => void) => () => void;
  onConfig: (callback: (config: MavlinkServiceConfig) => void) => () => void;
  getConfig: () => Promise<MavlinkServiceConfig>;
  setQgcForwarding: (enabled: boolean) => Promise<MavlinkServiceConfig>;
  setListenPort: (port: number) => Promise<MavlinkServiceConfig>;
  selectVehicle: (id: string) => Promise<SessionSnapshotPayload>;
  addMarker: (label: string) => Promise<SessionSnapshotPayload>;
};

const api: AltairVisualizerApi = {
  onVehicleState(callback) {
    const listener = (_event: Electron.IpcRendererEvent, message: VehicleStatePayload): void => callback(message);
    ipcRenderer.on('vehicle-state', listener);
    return () => ipcRenderer.off('vehicle-state', listener);
  },
  onSessionSnapshot(callback) {
    const listener = (_event: Electron.IpcRendererEvent, message: SessionSnapshotPayload): void => callback(message);
    ipcRenderer.on('session-snapshot', listener);
    return () => ipcRenderer.off('session-snapshot', listener);
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
  },
  selectVehicle(id) {
    return ipcRenderer.invoke('mavlink:select-vehicle', id);
  },
  addMarker(label) {
    return ipcRenderer.invoke('mavlink:add-marker', label);
  }
};

contextBridge.exposeInMainWorld('altairVisualizer', api);
