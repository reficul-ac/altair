import { app, BrowserWindow, dialog, ipcMain } from 'electron';
import { readFile, writeFile } from 'node:fs/promises';
import path from 'node:path';
import { fileURLToPath } from 'node:url';
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
import type { CommandAuditEntry, CommandDispatchResult, GuardedCommandRequest, GuardedCommandResult, MissionPlan, MockLinkState } from './state.js';

const __dirname = path.dirname(fileURLToPath(import.meta.url));
const appRoot = path.resolve(__dirname, '..');

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
    }
  }
  if (qgcEndpoints.length > 0) {
    config.qgcEndpoints = qgcEndpoints;
  }
  return config;
}

const telemetry = new MavlinkTelemetryService(parseArgs(process.argv.slice(1)));
const replay = new ReplaySession();
const commandAuditLog = createCommandAuditLog(path.join(app.getPath('userData'), 'command-audit.jsonl'));
let recentCommandAudit: CommandAuditEntry[] = [];
let mockLink: MockLinkState | null = null;

function sendToWindows(channel: string, payload: VehicleStatePayload | MavlinkServiceConfig | SessionSnapshotPayload | ReplayPlaybackState): void {
  const message = channel === 'session-snapshot' && isSessionSnapshot(payload) ? withRecentCommandAudit(payload) : payload;
  for (const window of BrowserWindow.getAllWindows()) {
    window.webContents.send(channel, message);
  }
}

function isSessionSnapshot(payload: VehicleStatePayload | MavlinkServiceConfig | SessionSnapshotPayload | ReplayPlaybackState): payload is SessionSnapshotPayload {
  return typeof payload === 'object' && payload !== null && 'type' in payload && payload.type === 'session_snapshot';
}

function withRecentCommandAudit(snapshot: SessionSnapshotPayload): SessionSnapshotPayload {
  return {
    ...snapshot,
    commandAudit: recentCommandAudit.slice(0, 20)
  };
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
    timestamp: new Date().toISOString(),
    vehicleId: request.vehicleId,
    commandName: request.command,
    commandId: spec?.command ?? null,
    params: spec?.params ?? [],
    confirmationType: request.confirmationType ?? 'browser-confirm',
    accepted: false,
    state: 'blocked',
    reason,
    ack: null,
    authority: writable ? 'sitl-writable' : 'read-only',
    writable,
    retryCount: 0
  };
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
      preload: path.join(__dirname, 'preload.js'),
      contextIsolation: true,
      nodeIntegration: false
    }
  });

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

telemetry.on('vehicle-state', (payload: VehicleStatePayload) => sendToWindows('vehicle-state', payload));
telemetry.on('config', (config: MavlinkServiceConfig) => sendToWindows('mavlink-config', config));
telemetry.on('session-snapshot', (snapshot: SessionSnapshotPayload) => sendToWindows('session-snapshot', snapshot));
telemetry.on('command-audit', (entry: CommandAuditEntry) => {
  void appendAudit(entry).catch((error) => console.error('Failed to append command audit entry', error));
});
replay.on('session-snapshot', (snapshot) => sendToWindows('session-snapshot', snapshot as SessionSnapshotPayload));
replay.on('state', (state) => sendToWindows('replay-state', state));

app.whenReady().then(async () => {
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
  app.exit(1);
});

app.on('window-all-closed', () => {
  if (process.platform !== 'darwin') {
    app.quit();
  }
});

app.on('before-quit', () => {
  void telemetry.stop();
});
