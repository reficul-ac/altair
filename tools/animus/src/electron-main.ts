import { app, BrowserWindow, dialog, ipcMain, net, protocol } from 'electron';
import { readFile, writeFile } from 'node:fs/promises';
import path from 'node:path';
import { createInterface } from 'node:readline';
import { fileURLToPath, pathToFileURL } from 'node:url';
import {
  commandSpec,
  DEFAULT_LISTEN_HOST,
  DEFAULT_LISTEN_PORT,
  DEFAULT_QGC_HOST,
  DEFAULT_QGC_PORT,
  MavlinkTelemetryService,
  parseEndpoint,
  type MavlinkServiceConfig,
  type SessionSnapshotPayload,
  type VehicleStatePayload
} from './mavlink.js';
import { parseAltairReplayJson, ReplaySession, type ReplayPlaybackState } from './replay.js';
import { createMockLink, defaultCommandCapabilities, evaluateGuardedCommand } from './parity.js';
import { validateMission } from './state.js';
import { createCommandAuditLog } from './command-audit.js';
import { animusSettingsPath, readAnimusSettings, writeAnimusSettings } from './animus-settings.js';
import { dashboardLayoutPath, exportDashboardProfile, importDashboardProfile, readDashboardLayout, resetDashboardLayout, writeDashboardLayout } from './dashboard-settings.js';
import { MapCacheManager } from './map-cache-node.js';
import { ANIMUS_MAP_CACHE_PROTOCOL, type MapCacheDownloadRequest, type MapCacheEstimateRequest } from './map-cache.js';
import type { AnimusUiSettings } from './animus-settings.js';
import type { AnimusDashboardLayout } from './dashboard-types.js';
import type { CommandAuditEntry, CommandDispatchResult, GuardedCommandRequest, GuardedCommandResult, MissionPlan, MockLinkState } from './state.js';
import type { OperationAuditEntry, ParameterEditRequest } from './state.js';

type LayoutRefreshMessage = {
  reason: string;
  width: number;
  height: number;
};
type WindowMessage = VehicleStatePayload | MavlinkServiceConfig | SessionSnapshotPayload | ReplayPlaybackState | LayoutRefreshMessage;

const __dirname = path.dirname(fileURLToPath(import.meta.url));
const appRoot = path.resolve(__dirname, '..');
const animusSessionContext = {
  sessionId: process.env.ANIMUS_SESSION_ID ?? `electron-${process.pid}-${Date.now().toString(36)}`,
  operatorId: process.env.USER ?? process.env.USERNAME ?? 'unknown',
  appSource: 'animus-electron',
  processSource: `pid:${process.pid}`
};

function parseArgs(argv: string[]): Partial<MavlinkServiceConfig> {
  const config: Partial<MavlinkServiceConfig> = {};
  const qgcEndpoints = [];
  for (let index = 0; index < argv.length; index += 1) {
    const arg = argv[index];
    if (arg === '--listen-host') {
      config.listenHost = argv[++index] ?? DEFAULT_LISTEN_HOST;
    } else if (arg === '--listen-port') {
      config.listenPort = Number(argv[++index] ?? DEFAULT_LISTEN_PORT);
    } else if (arg === '--qgc-endpoint') {
      qgcEndpoints.push(parseEndpoint(argv[++index] ?? `${DEFAULT_QGC_HOST}:${DEFAULT_QGC_PORT}`));
    } else if (arg === '--no-qgc') {
      config.qgcForwarding = false;
    } else if (arg === '--qgc') {
      config.qgcForwarding = true;
    } else if (arg === '--writable-animus') {
      config.writableAnimus = true;
      config.authorityMode = 'sitl-writable';
    } else if (arg === '--trusted-live-writable') {
      config.writableAnimus = true;
      config.authorityMode = 'trusted-live-writable';
    } else if (arg === '--maintenance-setup') {
      config.writableAnimus = true;
      config.authorityMode = 'maintenance-setup';
    }
  }
  if (qgcEndpoints.length > 0) {
    config.qgcEndpoints = qgcEndpoints;
  }
  return config;
}

const telemetry = new MavlinkTelemetryService({
  ...parseArgs(process.argv.slice(1)),
  logDownloadDirectory: path.join(app.getPath('userData'), 'onboard-logs')
}, animusSessionContext);
const replay = new ReplaySession();
const commandAuditLog = createCommandAuditLog(path.join(app.getPath('userData'), 'command-audit.jsonl'));
const operationAuditLog = createCommandAuditLog(path.join(app.getPath('userData'), 'operation-audit.jsonl'));
const dashboardLayoutFile = dashboardLayoutPath(app.getPath('userData'));
const animusSettingsFile = animusSettingsPath(app.getPath('userData'));
const mapCache = new MapCacheManager(app.getPath('userData'), 'tiles', async () => (await readAnimusSettings(animusSettingsFile)).activeMapCacheSetId);
const demCache = new MapCacheManager(app.getPath('userData'), 'dem', async () => (await readAnimusSettings(animusSettingsFile)).activeDemCacheSetId);
let recentCommandAudit: CommandAuditEntry[] = [];
let mockLink: MockLinkState | null = null;
let shutdownPromise: Promise<void> | null = null;
let exiting = false;

function sendToWindows(channel: string, payload: WindowMessage): void {
  const message = channel === 'session-snapshot' && isSessionSnapshot(payload) ? withRecentCommandAudit(payload) : payload;
  for (const window of BrowserWindow.getAllWindows()) {
    window.webContents.send(channel, message);
  }
}

function isSessionSnapshot(payload: WindowMessage): payload is SessionSnapshotPayload {
  return typeof payload === 'object' && payload !== null && 'type' in payload && payload.type === 'session_snapshot';
}

function withRecentCommandAudit(snapshot: SessionSnapshotPayload): SessionSnapshotPayload {
  return {
    ...snapshot,
    commandAudit: recentCommandAudit.slice(0, 20)
  };
}

async function stopRuntimeServices(): Promise<void> {
  await telemetry.stop();
}

async function shutdown(exitCode: number): Promise<void> {
  if (!shutdownPromise) {
    shutdownPromise = stopRuntimeServices();
  }
  try {
    await shutdownPromise;
  } catch (error) {
    console.error('Failed to stop Animus runtime services during shutdown', error);
    exitCode = exitCode === 0 ? 1 : exitCode;
  }
  exiting = true;
  process.exit(exitCode);
}

function requestShutdown(exitCode = 0): void {
  void shutdown(exitCode);
}

function installStdinShutdownHandler(): void {
  if (process.stdin.isTTY) {
    return;
  }
  const input = createInterface({ input: process.stdin });
  input.on('line', (line) => {
    if (line.trim() === 'shutdown') {
      input.close();
      requestShutdown(0);
    }
  });
}

async function appendAudit(entry: CommandAuditEntry): Promise<void> {
  recentCommandAudit = [entry, ...recentCommandAudit.filter((candidate) => candidate !== entry)].slice(0, 20);
  await commandAuditLog.append(entry);
}

function buildRejectedCommandAuditEntry(request: GuardedCommandRequest, reason: string, eventKind: CommandAuditEntry['eventKind']): CommandAuditEntry {
  const spec = commandSpec(request.command, request.params);
  const writable = telemetry.getConfig().writableAnimus;
  return {
    schemaVersion: 1,
    eventKind,
    transactionId: null,
    sessionId: animusSessionContext.sessionId,
    operatorId: animusSessionContext.operatorId,
    timestamp: new Date().toISOString(),
    vehicleId: request.vehicleId,
    commandName: request.command,
    commandId: spec?.command ?? null,
    params: spec?.params ?? [],
    payload: { params: { ...(request.params ?? {}) }, encodedParams: spec?.params ?? [] },
    confirmationType: request.confirmationType ?? 'browser-confirm',
    accepted: false,
    state: 'blocked',
    reason,
    failureReason: reason,
    ack: null,
    authority: telemetry.getConfig().authorityMode,
    writable,
    retryCount: 0,
    appSource: animusSessionContext.appSource,
    processSource: animusSessionContext.processSource,
    commandOrigin: request.originSurface ?? 'unknown',
    vehicleTarget: request.vehicleId,
    authorityMode: telemetry.getConfig().authorityMode,
    writableEndpoint: telemetry.linkState().blockedReason ? null : undefined,
    qgcForwarding: telemetry.getConfig().qgcForwarding,
    confirmationResult: request.confirmationResult ?? (request.confirmed ? 'accepted' : 'rejected')
  };
}

protocol.registerSchemesAsPrivileged([{ scheme: ANIMUS_MAP_CACHE_PROTOCOL, privileges: { standard: true, secure: true, supportFetchAPI: true } }]);

function registerMapCacheProtocol(): void {
  protocol.handle(ANIMUS_MAP_CACHE_PROTOCOL, async (request) => {
    const url = new URL(request.url);
    const [setId, z, x, y] = url.pathname.split('/').filter(Boolean);
    const manager = url.hostname === 'dem' ? demCache : mapCache;
    if ((url.hostname !== 'tiles' && url.hostname !== 'dem') || !setId || !z || !x || !y) {
      return new Response(mapCache.emptyTileBytes(), { headers: { 'content-type': 'image/png' } });
    }
    const tile = await manager.tilePath(decodeURIComponent(setId), z, x, y);
    if (!tile.found) {
      return new Response(mapCache.emptyTileBytes(), { headers: { 'content-type': 'image/png' } });
    }
    return net.fetch(pathToFileURL(tile.path).href);
  });
}

function createWindow(): BrowserWindow {
  const window = new BrowserWindow({
    width: 1280,
    height: 820,
    minWidth: 960,
    minHeight: 620,
    title: 'Altair Animus',
    backgroundColor: '#0b1116',
    webPreferences: {
      preload: path.join(__dirname, 'preload.cjs'),
      contextIsolation: true,
      nodeIntegration: false
    }
  });
  window.webContents.setZoomFactor(1);

  const notifyLayoutRefresh = (reason: string): void => {
    window.webContents.setZoomFactor(1);
    setTimeout(() => {
      if (window.isDestroyed() || window.webContents.isDestroyed()) return;
      const [width, height] = window.getContentSize();
      window.webContents.setZoomFactor(1);
      window.webContents.send('animus:layout-refresh', { reason, width, height });
    }, 80);
  };

  window.webContents.on('preload-error', (_event, preloadPath, error) => {
    console.error(`Electron preload failed (${preloadPath}):`, error);
  });
  window.webContents.on('render-process-gone', (_event, details) => {
    console.error(`Electron renderer exited: reason=${details.reason} exitCode=${details.exitCode}`);
  });
  window.webContents.on('unresponsive', () => {
    console.error('Electron renderer became unresponsive');
  });
  window.webContents.on('did-fail-load', (_event, errorCode, errorDescription, validatedURL) => {
    console.error(`Electron renderer failed to load ${validatedURL}: ${errorCode} ${errorDescription}`);
  });
  window.webContents.on('did-finish-load', () => notifyLayoutRefresh('did-finish-load'));
  window.on('enter-full-screen', () => notifyLayoutRefresh('enter-full-screen'));
  window.on('leave-full-screen', () => notifyLayoutRefresh('leave-full-screen'));
  window.on('resize', () => notifyLayoutRefresh('resize'));
  window.on('maximize', () => notifyLayoutRefresh('maximize'));
  window.on('restore', () => notifyLayoutRefresh('restore'));

  if (process.env.VITE_DEV_SERVER_URL) {
    void window.loadURL(process.env.VITE_DEV_SERVER_URL);
  } else {
    void window.loadFile(path.join(appRoot, 'dist', 'index.html'));
  }
  return window;
}

ipcMain.handle('mavlink:get-config', () => telemetry.getConfig());
ipcMain.handle('mavlink:set-qgc-forwarding', (_event, enabled: boolean) => {
  telemetry.setQgcForwarding(Boolean(enabled));
  return telemetry.getConfig();
});
ipcMain.handle('mavlink:set-listen-port', async (_event, port: number) => {
  await telemetry.setListenPort(Number(port));
  return telemetry.getConfig();
});
ipcMain.handle('mavlink:select-vehicle', (_event, id: string) => replay.isLoaded() ? replay.selectVehicle(String(id)) : telemetry.selectVehicle(String(id)));
ipcMain.handle('mavlink:add-marker', (_event, label: string) => telemetry.addMarker(String(label || 'Marker')));
ipcMain.handle('settings:get', () => readAnimusSettings(animusSettingsFile));
ipcMain.handle('settings:save', (_event, settings: AnimusUiSettings) => writeAnimusSettings(animusSettingsFile, settings));
ipcMain.handle('map-cache:status', async () => mapCache.status());
ipcMain.handle('map-cache:list', async () => (await mapCache.status()).sets);
ipcMain.handle('map-cache:estimate', async (_event, request: MapCacheEstimateRequest) => mapCache.estimate(request));
ipcMain.handle('map-cache:start-download', async (_event, request: MapCacheDownloadRequest) => mapCache.startDownload(request));
ipcMain.handle('map-cache:cancel-download', async () => mapCache.cancelDownload());
ipcMain.handle('map-cache:activate', async (_event, setId: string) => mapCache.activate(String(setId)));
ipcMain.handle('map-cache:delete', async (_event, setId: string) => mapCache.delete(String(setId)));
ipcMain.handle('dem-cache:status', async () => demCache.status());
ipcMain.handle('dem-cache:list', async () => (await demCache.status()).sets);
ipcMain.handle('dem-cache:estimate', async (_event, request: MapCacheEstimateRequest) => demCache.estimate(request));
ipcMain.handle('dem-cache:start-download', async (_event, request: MapCacheDownloadRequest) => demCache.startDownload(request));
ipcMain.handle('dem-cache:cancel-download', async () => demCache.cancelDownload());
ipcMain.handle('dem-cache:activate', async (_event, setId: string) => demCache.activate(String(setId)));
ipcMain.handle('dem-cache:delete', async (_event, setId: string) => demCache.delete(String(setId)));
ipcMain.handle('dashboard:get-layout', () => readDashboardLayout(dashboardLayoutFile));
ipcMain.handle('dashboard:save-layout', (_event, layout: AnimusDashboardLayout) => writeDashboardLayout(dashboardLayoutFile, layout));
ipcMain.handle('dashboard:reset-layout', () => resetDashboardLayout(dashboardLayoutFile));
ipcMain.handle('dashboard:export-profile', async (_event, layout: AnimusDashboardLayout) => {
  const result = await dialog.showSaveDialog({
    title: 'Export dashboard profile',
    defaultPath: 'altair-dashboard-profile.json',
    filters: [{ name: 'Dashboard profile', extensions: ['json'] }]
  });
  if (result.canceled || !result.filePath) return { saved: false };
  await exportDashboardProfile(result.filePath, layout);
  return { saved: true, path: result.filePath };
});
ipcMain.handle('dashboard:import-profile', async () => {
  const result = await dialog.showOpenDialog({
    title: 'Import dashboard profile',
    properties: ['openFile'],
    filters: [{ name: 'Dashboard profile', extensions: ['json'] }]
  });
  if (result.canceled || !result.filePaths[0]) return { imported: false };
  const layout = await importDashboardProfile(result.filePaths[0]);
  return { imported: true, path: result.filePaths[0], layout };
});
ipcMain.handle('mission:validate', (_event, plan: MissionPlan) => validateMission(plan));
ipcMain.handle('mission:save', async (_event, plan: MissionPlan) => {
  const validation = validateMission(plan);
  if (!validation.valid) return { saved: false, validation, reason: 'mission validation failed' };
  const result = await dialog.showSaveDialog({
    title: 'Save Bayek mission',
    defaultPath: 'bayek-mission.json',
    filters: [{ name: 'Bayek mission', extensions: ['json'] }]
  });
  if (result.canceled || !result.filePath) return { saved: false, validation };
  await writeFile(result.filePath, JSON.stringify(plan, null, 2), 'utf8');
  return { saved: true, path: result.filePath, validation };
});
ipcMain.handle('mission:load', async () => {
  const result = await dialog.showOpenDialog({
    title: 'Load Bayek mission',
    properties: ['openFile'],
    filters: [{ name: 'Bayek mission', extensions: ['json'] }]
  });
  if (result.canceled || !result.filePaths[0]) return { loaded: false };
  const plan = JSON.parse(await readFile(result.filePaths[0], 'utf8')) as MissionPlan;
  return { loaded: true, path: result.filePaths[0], plan, validation: validateMission(plan) };
});
ipcMain.handle('mission:upload-sitl', (_event, plan: MissionPlan, vehicleId?: string) => telemetry.uploadMissionToSitl(plan, vehicleId));
ipcMain.handle('mission:download-sitl', (_event, vehicleId?: string) => telemetry.downloadMissionFromSitl(vehicleId));
ipcMain.handle('parameters:refresh', (_event, vehicleId?: string) => telemetry.refreshParameters(vehicleId));
ipcMain.handle('parameters:set', (_event, request: ParameterEditRequest) => telemetry.setParameter(request));
ipcMain.handle('mission:upload', (_event, plan: MissionPlan, vehicleId?: string, confirmed?: boolean) => telemetry.uploadMission(plan, vehicleId, Boolean(confirmed)));
ipcMain.handle('mission:download', (_event, vehicleId?: string) => telemetry.downloadMission(vehicleId));
ipcMain.handle('mission:clear', (_event, vehicleId?: string, confirmed?: boolean) => telemetry.clearMission(vehicleId, Boolean(confirmed)));
ipcMain.handle('logs:list-onboard', (_event, vehicleId?: string) => telemetry.listLogs(vehicleId));
ipcMain.handle('logs:download-onboard', async (_event, logId: number, vehicleId?: string) => telemetry.downloadLog(Number(logId), vehicleId));
ipcMain.handle('logs:erase-onboard', (_event, vehicleId?: string, confirmed?: boolean) => telemetry.eraseLogs(vehicleId, Boolean(confirmed)));
ipcMain.handle('terrain:request', (_event, vehicleId?: string) => telemetry.requestTerrain(vehicleId));
ipcMain.handle('terrain:check', (_event, vehicleId?: string) => telemetry.requestTerrain(vehicleId));
ipcMain.handle('camera:capture', (_event, vehicleId?: string) => telemetry.cameraAction('capture', vehicleId));
ipcMain.handle('camera:record', (_event, recording: boolean, vehicleId?: string) => telemetry.cameraAction(recording ? 'record-start' : 'record-stop', vehicleId));
ipcMain.handle('camera:set-setting', (_event, setting: 'zoom' | 'focus', value: number, vehicleId?: string) => telemetry.cameraAction(setting === 'focus' ? 'focus' : 'zoom', vehicleId, Number(value)));
ipcMain.handle('replay:open', async () => {
  const result = await dialog.showOpenDialog({
    title: 'Open Altair replay',
    properties: ['openFile'],
    filters: [{ name: 'Altair replay', extensions: ['altair-replay', 'json'] }]
  });
  if (result.canceled || !result.filePaths[0]) {
    return replay.state();
  }
  const raw = await readFile(result.filePaths[0], 'utf8');
  return replay.load(parseAltairReplayJson(raw));
});
ipcMain.handle('logs:import', async () => {
  const result = await dialog.showOpenDialog({
    title: 'Import log for offline analysis',
    properties: ['openFile'],
    filters: [
      { name: 'Logs', extensions: ['altair-replay', 'json', 'ulg', 'ulog', 'csv', 'tsv'] },
      { name: 'All files', extensions: ['*'] }
    ]
  });
  if (result.canceled || !result.filePaths[0]) return replay.state();
  const raw = await readFile(result.filePaths[0], 'utf8');
  const { importLogAsReplay } = await import('./replay.js');
  const ext = path.extname(result.filePaths[0]).toLowerCase();
  const sourceType = ext === '.ulg' || ext === '.ulog' ? 'ulog-import' : ext === '.csv' || ext === '.tsv' ? 'csv-import' : 'altair-session';
  return replay.load(importLogAsReplay({ name: path.basename(result.filePaths[0]), sourceType, text: raw, importedAt: new Date().toISOString() }));
});
ipcMain.handle('logs:export-session', async () => {
  const currentReplay = replay.exportReplay();
  if (!currentReplay) return { saved: false, reason: 'no replay is loaded' };
  const result = await dialog.showSaveDialog({
    title: 'Save replay timeline',
    defaultPath: 'altair-session.altair-replay',
    filters: [{ name: 'Altair replay', extensions: ['altair-replay', 'json'] }]
  });
  if (result.canceled || !result.filePath) return { saved: false };
  await writeFile(result.filePath, JSON.stringify(currentReplay, null, 2), 'utf8');
  return { saved: true, path: result.filePath };
});
ipcMain.handle('replay:play', () => replay.play());
ipcMain.handle('replay:pause', () => replay.pause());
ipcMain.handle('replay:seek', (_event, timestampS: number) => replay.seek(Number(timestampS)));
ipcMain.handle('replay:set-speed', (_event, speed: number) => replay.setSpeed(Number(speed)));
ipcMain.handle('replay:reset', () => replay.reset());
ipcMain.handle('replay:marker', (_event, direction: -1 | 1) => replay.seekMarker(direction < 0 ? -1 : 1));
ipcMain.handle('mock-link:start', (_event, vehicleCount: number) => {
  mockLink = createMockLink(Number(vehicleCount));
  const activeMockLink = mockLink;
  const snapshot: SessionSnapshotPayload = {
    type: 'session_snapshot',
    vehicles: [],
    selectedVehicleId: null,
    messages: [],
    events: [{ id: 'mock-link-started', timestampS: 0, vehicleId: null, level: 'info', kind: 'mock-link', label: activeMockLink.label, position: null }],
    packetCount: 0,
    decodedCount: 0,
    mockLinks: [activeMockLink]
  };
  sendToWindows('session-snapshot', snapshot);
  return mockLink;
});
ipcMain.handle('command:issue', async (_event, request: GuardedCommandRequest): Promise<GuardedCommandResult | CommandDispatchResult> => {
  const selected = telemetry.registry.selectedVehicle();
  const capability = selected.commandCapabilities ?? defaultCommandCapabilities(selected.connected, telemetry.getConfig().writableAnimus);
  const guard = evaluateGuardedCommand(request, capability);
  const result = guard.accepted ? telemetry.dispatchCommand(request) : guard;
  if (!result.accepted) {
    if (!guard.accepted) {
      await appendAudit(buildRejectedCommandAuditEntry(request, result.reason, request.confirmed ? 'guard-rejected' : 'confirmation-rejected'));
    }
    sendToWindows('session-snapshot', {
      type: 'session_snapshot',
      vehicles: [],
      selectedVehicleId: null,
      messages: [],
      events: [{ id: `command-${Date.now()}`, timestampS: 0, vehicleId: request.vehicleId, level: 'info', kind: 'command', label: result.reason, position: null }],
      packetCount: 0,
      decodedCount: 0,
      mockLinks: mockLink ? [mockLink] : []
    } as SessionSnapshotPayload);
  }
  return result;
});
ipcMain.handle('command:audit-rejection', async (_event, request: GuardedCommandRequest, reason?: string) => {
  const entry = buildRejectedCommandAuditEntry(request, String(reason || 'operator confirmation was rejected'), 'confirmation-rejected');
  await appendAudit(entry);
  sendToWindows('session-snapshot', {
    type: 'session_snapshot',
    vehicles: [],
    selectedVehicleId: null,
    messages: [],
    events: [{ id: `command-${Date.now()}`, timestampS: 0, vehicleId: request.vehicleId, level: 'info', kind: 'command', label: entry.reason, position: null }],
    packetCount: 0,
    decodedCount: 0,
    mockLinks: mockLink ? [mockLink] : []
  } as SessionSnapshotPayload);
  return entry;
});
ipcMain.handle('command:cancel', (_event, transactionId: string) => telemetry.cancelCommandTransaction(String(transactionId)));
ipcMain.handle('command:retry', (_event, transactionId: string) => telemetry.retryCommandTransaction(String(transactionId)));

telemetry.on('vehicle-state', (payload: VehicleStatePayload) => sendToWindows('vehicle-state', payload));
telemetry.on('config', (config: MavlinkServiceConfig) => sendToWindows('mavlink-config', config));
telemetry.on('session-snapshot', (snapshot: SessionSnapshotPayload) => sendToWindows('session-snapshot', snapshot));
telemetry.on('command-audit', (entry: CommandAuditEntry) => {
  void appendAudit(entry).catch((error) => console.error('Failed to append command audit entry', error));
});
telemetry.on('operation-audit', (entry: OperationAuditEntry) => {
  void operationAuditLog.append(entry as unknown as CommandAuditEntry).catch((error) => console.error('Failed to append operation audit entry', error));
});
replay.on('session-snapshot', (snapshot) => sendToWindows('session-snapshot', snapshot as SessionSnapshotPayload));
replay.on('state', (state) => sendToWindows('replay-state', state));

app.whenReady().then(async () => {
  registerMapCacheProtocol();
  recentCommandAudit = await commandAuditLog.recent(20);
  await telemetry.start();
  createWindow();

  app.on('activate', () => {
    if (BrowserWindow.getAllWindows().length === 0) {
      createWindow();
    }
  });
}).catch((error) => {
  console.error(error);
  exiting = true;
  app.exit(1);
});

app.on('window-all-closed', () => {
  if (process.platform !== 'darwin') {
    app.quit();
  }
});

app.on('before-quit', (event) => {
  if (exiting) {
    return;
  }
  event.preventDefault();
  requestShutdown(0);
});

process.on('SIGINT', () => requestShutdown(0));
process.on('SIGTERM', () => requestShutdown(0));
installStdinShutdownHandler();
