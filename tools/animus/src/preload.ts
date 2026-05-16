import { contextBridge, ipcRenderer } from 'electron';
import type { MavlinkServiceConfig, SessionSnapshotPayload, VehicleStatePayload } from './mavlink.js';
import type { ReplayTimelineMessage } from './state.js';
import type { CommandAuditEntry, CommandDispatchResult, CommandTransaction, GuardedCommandRequest, GuardedCommandResult, MissionPlan, MissionTransferState, MissionValidationResult, MockLinkState } from './state.js';

export type AltairAnimusApi = {
  onVehicleState: (callback: (message: VehicleStatePayload) => void) => () => void;
  onSessionSnapshot: (callback: (message: SessionSnapshotPayload) => void) => () => void;
  onConfig: (callback: (config: MavlinkServiceConfig) => void) => () => void;
  getConfig: () => Promise<MavlinkServiceConfig>;
  setQgcForwarding: (enabled: boolean) => Promise<MavlinkServiceConfig>;
  setListenPort: (port: number) => Promise<MavlinkServiceConfig>;
  selectVehicle: (id: string) => Promise<SessionSnapshotPayload>;
  addMarker: (label: string) => Promise<SessionSnapshotPayload>;
  validateMission: (plan: MissionPlan) => Promise<MissionValidationResult>;
  saveMission: (plan: MissionPlan) => Promise<{ saved: boolean; path?: string; reason?: string; validation: MissionValidationResult }>;
  loadMission: () => Promise<{ loaded: boolean; path?: string; plan?: MissionPlan; validation?: MissionValidationResult }>;
  uploadMissionToSitl: (plan: MissionPlan, vehicleId?: string) => Promise<MissionTransferState>;
  downloadMissionFromSitl: (vehicleId?: string) => Promise<MissionTransferState>;
  onReplayState: (callback: (message: ReplayTimelineMessage) => void) => () => void;
  openReplay: () => Promise<ReplayTimelineMessage>;
  importLog: () => Promise<ReplayTimelineMessage>;
  exportSessionLog: () => Promise<{ saved: boolean; path?: string }>;
  startMockLink: (vehicleCount: number) => Promise<MockLinkState>;
  issueCommand: (request: GuardedCommandRequest) => Promise<GuardedCommandResult | CommandDispatchResult>;
  cancelCommand: (transactionId: string) => Promise<CommandTransaction | null>;
  retryCommand: (transactionId: string) => Promise<CommandDispatchResult | null>;
  auditCommandRejection: (request: GuardedCommandRequest, reason: string) => Promise<CommandAuditEntry>;
  replayPlay: () => Promise<ReplayTimelineMessage>;
  replayPause: () => Promise<ReplayTimelineMessage>;
  replaySeek: (timestampS: number) => Promise<ReplayTimelineMessage>;
  replaySetSpeed: (speed: number) => Promise<ReplayTimelineMessage>;
  replayReset: () => Promise<ReplayTimelineMessage>;
  replayMarker: (direction: -1 | 1) => Promise<ReplayTimelineMessage>;
};

const api: AltairAnimusApi = {
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
  onReplayState(callback) {
    const listener = (_event: Electron.IpcRendererEvent, message: ReplayTimelineMessage): void => callback(message);
    ipcRenderer.on('replay-state', listener);
    return () => ipcRenderer.off('replay-state', listener);
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
  },
  validateMission(plan) {
    return ipcRenderer.invoke('mission:validate', plan);
  },
  saveMission(plan) {
    return ipcRenderer.invoke('mission:save', plan);
  },
  loadMission() {
    return ipcRenderer.invoke('mission:load');
  },
  uploadMissionToSitl(plan, vehicleId) {
    return ipcRenderer.invoke('mission:upload-sitl', plan, vehicleId);
  },
  downloadMissionFromSitl(vehicleId) {
    return ipcRenderer.invoke('mission:download-sitl', vehicleId);
  },
  openReplay() {
    return ipcRenderer.invoke('replay:open');
  },
  importLog() {
    return ipcRenderer.invoke('logs:import');
  },
  exportSessionLog() {
    return ipcRenderer.invoke('logs:export-session');
  },
  startMockLink(vehicleCount) {
    return ipcRenderer.invoke('mock-link:start', vehicleCount);
  },
  issueCommand(request) {
    return ipcRenderer.invoke('command:issue', request);
  },
  cancelCommand(transactionId) {
    return ipcRenderer.invoke('command:cancel', transactionId);
  },
  retryCommand(transactionId) {
    return ipcRenderer.invoke('command:retry', transactionId);
  },
  auditCommandRejection(request, reason) {
    return ipcRenderer.invoke('command:audit-rejection', request, reason);
  },
  replayPlay() {
    return ipcRenderer.invoke('replay:play');
  },
  replayPause() {
    return ipcRenderer.invoke('replay:pause');
  },
  replaySeek(timestampS) {
    return ipcRenderer.invoke('replay:seek', timestampS);
  },
  replaySetSpeed(speed) {
    return ipcRenderer.invoke('replay:set-speed', speed);
  },
  replayReset() {
    return ipcRenderer.invoke('replay:reset');
  },
  replayMarker(direction) {
    return ipcRenderer.invoke('replay:marker', direction);
  }
};

contextBridge.exposeInMainWorld('altairAnimus', api);
