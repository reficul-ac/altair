import { app, BrowserWindow, ipcMain } from 'electron';
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

function sendToWindows(channel: string, payload: VehicleStatePayload | MavlinkServiceConfig | SessionSnapshotPayload): void {
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
ipcMain.handle('mavlink:select-vehicle', (_event, id: string) => telemetry.selectVehicle(String(id)));
ipcMain.handle('mavlink:add-marker', (_event, label: string) => telemetry.addMarker(String(label || 'Marker')));

telemetry.on('vehicle-state', (payload: VehicleStatePayload) => sendToWindows('vehicle-state', payload));
telemetry.on('config', (config: MavlinkServiceConfig) => sendToWindows('mavlink-config', config));
telemetry.on('session-snapshot', (snapshot: SessionSnapshotPayload) => sendToWindows('session-snapshot', snapshot));

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
