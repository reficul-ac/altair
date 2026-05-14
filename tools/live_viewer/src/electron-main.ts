import { app, BrowserWindow, dialog, ipcMain } from 'electron';
import { readFile } from 'node:fs/promises';
import path from 'node:path';
import { fileURLToPath } from 'node:url';
import {
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
    }
  }
  if (qgcEndpoints.length > 0) {
    config.qgcEndpoints = qgcEndpoints;
  }
  return config;
}

const telemetry = new MavlinkTelemetryService(parseArgs(process.argv.slice(1)));
const replay = new ReplaySession();

function sendToWindows(channel: string, payload: VehicleStatePayload | MavlinkServiceConfig | SessionSnapshotPayload | ReplayPlaybackState): void {
  for (const window of BrowserWindow.getAllWindows()) {
    window.webContents.send(channel, payload);
  }
}

function createWindow(): BrowserWindow {
  const window = new BrowserWindow({
    width: 1280,
    height: 820,
    minWidth: 960,
    minHeight: 620,
    title: 'Altair Visualizer',
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
ipcMain.handle('replay:play', () => replay.play());
ipcMain.handle('replay:pause', () => replay.pause());
ipcMain.handle('replay:seek', (_event, timestampS: number) => replay.seek(Number(timestampS)));
ipcMain.handle('replay:set-speed', (_event, speed: number) => replay.setSpeed(Number(speed)));
ipcMain.handle('replay:reset', () => replay.reset());
ipcMain.handle('replay:marker', (_event, direction: -1 | 1) => replay.seekMarker(direction < 0 ? -1 : 1));

telemetry.on('vehicle-state', (payload: VehicleStatePayload) => sendToWindows('vehicle-state', payload));
telemetry.on('config', (config: MavlinkServiceConfig) => sendToWindows('mavlink-config', config));
telemetry.on('session-snapshot', (snapshot: SessionSnapshotPayload) => sendToWindows('session-snapshot', snapshot));
replay.on('session-snapshot', (snapshot) => sendToWindows('session-snapshot', snapshot as SessionSnapshotPayload));
replay.on('state', (state) => sendToWindows('replay-state', state));

app.whenReady().then(async () => {
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
