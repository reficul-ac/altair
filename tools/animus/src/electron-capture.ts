import { app, BrowserWindow, ipcMain, net, protocol, type Event, type WebContentsConsoleMessageEventParams } from 'electron';
import { mkdir, writeFile } from 'node:fs/promises';
import path from 'node:path';
import { fileURLToPath, pathToFileURL } from 'node:url';
import { MapCacheManager } from './map-cache-node.js';
import { ANIMUS_MAP_CACHE_PROTOCOL } from './map-cache.js';
import { animusSettingsPath, readAnimusSettings } from './animus-settings.js';

type CaptureArgs = {
  url: string;
  outDir: string;
  workspaces: string[];
  viewports: { width: number; height: number; label: string }[];
  waitTimeoutMs: number;
  settleMs: number;
  captureTimeoutMs: number;
  fullscreen: boolean;
  debug: boolean;
};

type CaptureResult = {
  workspace: string;
  viewport: string;
  path: string;
  liveTelemetry: boolean;
  diagnostics: Record<string, unknown>;
};

type DebugLog = ((phase: string, event: string, fields?: Record<string, unknown>) => void) & {
  enabled: boolean;
};

const __dirname = path.dirname(fileURLToPath(import.meta.url));

protocol.registerSchemesAsPrivileged([{ scheme: ANIMUS_MAP_CACHE_PROTOCOL, privileges: { standard: true, secure: true, supportFetchAPI: true } }]);

function parseArgs(argv: string[]): CaptureArgs {
  const args: CaptureArgs = {
    url: 'http://127.0.0.1:5173',
    outDir: path.resolve('artifacts', 'animus-screenshots'),
    workspaces: ['flight', 'map', 'inspector'],
    viewports: [{ width: 1440, height: 900, label: '1440x900' }],
    waitTimeoutMs: 15000,
    settleMs: 800,
    captureTimeoutMs: 60000,
    fullscreen: false,
    debug: false
  };

  for (let index = 0; index < argv.length; index += 1) {
    const arg = argv[index];
    if (arg === '--url') {
      args.url = argv[++index] ?? args.url;
    } else if (arg === '--out-dir') {
      args.outDir = path.resolve(argv[++index] ?? args.outDir);
    } else if (arg === '--workspaces') {
      args.workspaces = parseList(argv[++index] ?? '').filter(Boolean);
    } else if (arg === '--viewports') {
      args.viewports = parseList(argv[++index] ?? '').map(parseViewport);
    } else if (arg === '--wait-timeout-ms') {
      args.waitTimeoutMs = Number(argv[++index] ?? args.waitTimeoutMs);
    } else if (arg === '--settle-ms') {
      args.settleMs = Number(argv[++index] ?? args.settleMs);
    } else if (arg === '--capture-timeout-ms') {
      args.captureTimeoutMs = Number(argv[++index] ?? args.captureTimeoutMs);
    } else if (arg === '--fullscreen') {
      args.fullscreen = true;
    } else if (arg === '--debug') {
      args.debug = true;
    } else if (arg === '--') {
      continue;
    }
  }

  if (args.workspaces.length === 0) {
    throw new Error('at least one workspace is required');
  }
  if (args.viewports.length === 0) {
    throw new Error('at least one viewport is required');
  }
  if (!Number.isFinite(args.captureTimeoutMs) || args.captureTimeoutMs <= 0) {
    throw new Error('capture timeout must be positive');
  }
  return args;
}

function parseList(text: string): string[] {
  return text.split(',').map((item) => item.trim());
}

function parseViewport(text: string): { width: number; height: number; label: string } {
  const match = /^(\d+)x(\d+)$/i.exec(text);
  if (!match) {
    throw new Error(`invalid viewport ${text}; expected WIDTHxHEIGHT`);
  }
  const width = Number(match[1]);
  const height = Number(match[2]);
  if (width < 320 || height < 320) {
    throw new Error(`viewport ${text} is too small`);
  }
  return { width, height, label: `${width}x${height}` };
}

function sleep(ms: number): Promise<void> {
  return new Promise((resolve) => setTimeout(resolve, ms));
}

function waitForWindowEvent(window: BrowserWindow, eventName: string, timeoutMs: number): Promise<void> {
  return new Promise((resolve, reject) => {
    const timer = setTimeout(() => {
      window.off(eventName as never, onEvent);
      reject(new Error(`timed out waiting for ${eventName}`));
    }, timeoutMs);
    const onEvent = () => {
      clearTimeout(timer);
      resolve();
    };
    window.once(eventName as never, onEvent);
  });
}

function createDebugLog(enabled: boolean): DebugLog {
  const log = (phase: string, event: string, fields: Record<string, unknown> = {}) => {
    if (!enabled) return;
    console.log(JSON.stringify({
      ts: new Date().toISOString(),
      level: 'debug',
      phase,
      event,
      ...fields
    }));
  };
  return Object.assign(log, { enabled });
}

async function rendererSnapshot(window: BrowserWindow): Promise<Record<string, unknown>> {
  let renderer: Record<string, unknown>;
  try {
    renderer = await window.webContents.executeJavaScript(`
      (() => {
        const status = document.querySelector('#status');
        const vehicle = document.querySelector('#vehicle-id');
        const rectOf = (selector) => {
          const node = document.querySelector(selector);
          if (!node) return null;
          const rect = node.getBoundingClientRect();
          return {
            x: rect.x,
            y: rect.y,
            width: rect.width,
            height: rect.height,
            top: rect.top,
            right: rect.right,
            bottom: rect.bottom,
            left: rect.left
          };
        };
        const scene = document.querySelector('#scene');
        const activeWorkspace = document.querySelector('[data-workspace].active')?.dataset.workspace || null;
        const visiblePanel = document.querySelector('.workspace-visible')?.getAttribute('data-panel') || null;
        const canvases = [...document.querySelectorAll('canvas')].map((canvas) => ({
          id: canvas.id,
          width: canvas.width,
          height: canvas.height,
          clientWidth: canvas.clientWidth,
          clientHeight: canvas.clientHeight
        }));
        const liveTelemetry = Boolean(status?.classList.contains('online')) ||
          /Connected/.test(status?.textContent || '') ||
          !/^sys\\s+--$/.test((vehicle?.textContent || '').trim());
        return {
          url: window.location.href,
          innerWidth: window.innerWidth,
          innerHeight: window.innerHeight,
          devicePixelRatio: window.devicePixelRatio,
          visualViewport: window.visualViewport ? {
            width: window.visualViewport.width,
            height: window.visualViewport.height,
            scale: window.visualViewport.scale,
            offsetLeft: window.visualViewport.offsetLeft,
            offsetTop: window.visualViewport.offsetTop
          } : null,
          readyState: document.readyState,
          title: document.title,
          shell: Boolean(document.querySelector('.shell')),
          shellRect: rectOf('.shell'),
          viewportRect: rectOf('.viewport'),
          sceneRect: rectOf('#scene'),
          sceneClient: scene ? {
            width: scene.clientWidth,
            height: scene.clientHeight,
            canvasWidth: scene.width,
            canvasHeight: scene.height
          } : null,
          statusText: (status?.textContent || '').trim(),
          statusClasses: status ? [...status.classList] : [],
          vehicleId: (vehicle?.textContent || '').trim(),
          activeWorkspace,
          visiblePanel,
          canvasCount: canvases.length,
          canvases,
          liveTelemetry
        };
      })()
    `) as Record<string, unknown>;
  } catch (error) {
    renderer = {
      rendererSnapshotError: error instanceof Error ? error.message : String(error)
    };
  }
  return {
    ...renderer,
    isLoading: window.webContents.isLoading(),
    isCrashed: window.webContents.isCrashed(),
    contentSize: window.getContentSize(),
    webContentsSize: window.getContentSize(),
    isFullScreen: window.isFullScreen(),
    focused: window.isFocused(),
    visible: window.isVisible()
  };
}

function nestedNumber(value: Record<string, unknown>, pathParts: string[]): number | null {
  let current: unknown = value;
  for (const key of pathParts) {
    if (typeof current !== 'object' || current === null || !(key in current)) return null;
    current = (current as Record<string, unknown>)[key];
  }
  return typeof current === 'number' && Number.isFinite(current) ? current : null;
}

function assertFullscreenLayout(snapshot: Record<string, unknown>): void {
  const innerWidth = nestedNumber(snapshot, ['innerWidth']);
  const shellWidth = nestedNumber(snapshot, ['shellRect', 'width']);
  const sceneWidth = nestedNumber(snapshot, ['sceneClient', 'width']);
  const sceneHeight = nestedNumber(snapshot, ['sceneClient', 'height']);
  if (innerWidth === null || shellWidth === null || sceneWidth === null || sceneHeight === null) {
    throw new Error(`fullscreen layout diagnostics were incomplete: ${JSON.stringify(snapshot)}`);
  }
  if (Math.abs(shellWidth - innerWidth) > 4) {
    throw new Error(`fullscreen shell width ${shellWidth} did not match window innerWidth ${innerWidth}`);
  }
  if (sceneWidth <= 900 || sceneHeight <= 820) {
    throw new Error(`fullscreen scene stayed near startup size: ${sceneWidth}x${sceneHeight}`);
  }
}

function startDebugSnapshots(
  window: BrowserWindow,
  debugLog: DebugLog,
  phase: string
): NodeJS.Timeout | undefined {
  if (!debugLog.enabled) return undefined;
  const capture = () => {
    rendererSnapshot(window)
      .then((snapshot) => debugLog(phase, 'state', snapshot))
      .catch((error) => {
        debugLog(phase, 'state-failure', {
          error: error instanceof Error ? error.message : String(error)
        });
      });
  };
  capture();
  const timer = setInterval(capture, 1000);
  timer.unref();
  return timer;
}

function wireDebugDiagnostics(window: BrowserWindow, debugLog: DebugLog): void {
  window.webContents.on('console-message', (details: Event<WebContentsConsoleMessageEventParams>) => {
    debugLog('renderer', 'console-message', {
      level: details.level,
      message: details.message,
      line: details.lineNumber,
      sourceId: details.sourceId
    });
  });
  window.webContents.on('did-fail-load', (_event, errorCode, errorDescription, validatedURL, isMainFrame) => {
    debugLog('loadURL', 'did-fail-load', { errorCode, errorDescription, validatedURL, isMainFrame });
  });
  window.webContents.on('render-process-gone', (_event, details) => {
    debugLog('renderer', 'render-process-gone', details as unknown as Record<string, unknown>);
  });
  window.on('unresponsive', () => {
    debugLog('browser-window', 'unresponsive');
  });
  window.webContents.on('preload-error', (_event, preloadPath, error) => {
    debugLog('renderer', 'preload-error', {
      preloadPath,
      error: error instanceof Error ? error.message : String(error)
    });
  });
  window.webContents.on('did-start-loading', () => {
    debugLog('loadURL', 'did-start-loading');
  });
  window.webContents.on('did-stop-loading', () => {
    debugLog('loadURL', 'did-stop-loading');
  });
  window.webContents.on('did-finish-load', () => {
    debugLog('loadURL', 'did-finish-load');
  });
  window.webContents.on('dom-ready', () => {
    debugLog('loadURL', 'dom-ready');
  });
}

async function waitForApp(
  window: BrowserWindow,
  timeoutMs: number,
  debugLog: DebugLog
): Promise<boolean> {
  const deadline = Date.now() + timeoutMs;
  let sawLiveTelemetry = false;
  debugLog('readiness-polling', 'start', { timeoutMs });
  const snapshots = startDebugSnapshots(window, debugLog, 'readiness-polling');
  while (Date.now() < deadline) {
    const state = await window.webContents.executeJavaScript(`
      (() => {
        const shell = Boolean(document.querySelector('.shell'));
        const status = document.querySelector('#status');
        const vehicle = document.querySelector('#vehicle-id');
        const liveTelemetry = Boolean(status?.classList.contains('online')) ||
          /Connected/.test(status?.textContent || '') ||
          !/^sys\\s+--$/.test((vehicle?.textContent || '').trim());
        return { shell, liveTelemetry };
      })()
    `) as { shell: boolean; liveTelemetry: boolean };
    sawLiveTelemetry = sawLiveTelemetry || state.liveTelemetry;
    if (state.shell && sawLiveTelemetry) {
      clearInterval(snapshots);
      debugLog('readiness-polling', 'end', { sawLiveTelemetry, ready: true });
      return true;
    }
    await sleep(250);
  }
  clearInterval(snapshots);
  debugLog('readiness-polling', 'end', { sawLiveTelemetry, ready: false });
  return sawLiveTelemetry;
}

async function selectWorkspace(
  window: BrowserWindow,
  workspace: string,
  debugLog: DebugLog
): Promise<void> {
  const workspaceName = JSON.stringify(workspace);
  debugLog('workspace-selection', 'start', { workspace });
  const snapshots = startDebugSnapshots(window, debugLog, 'workspace-selection');
  try {
    const selected = await window.webContents.executeJavaScript(`
      (() => {
        if (typeof window.__animusSetWorkspace === 'function') {
          window.__animusSetWorkspace(${workspaceName});
        } else {
          const button = [...document.querySelectorAll('[data-workspace]')]
            .find((candidate) => candidate.dataset.workspace === ${workspaceName});
          if (!button) return false;
          button.click();
        }
        const active = document.querySelector('[data-workspace].active')?.dataset.workspace;
        const panel = document.querySelector('[data-panel="${workspace}"]');
        return active === ${workspaceName} && Boolean(panel?.classList.contains('workspace-visible'));
      })()
    `) as boolean;
    if (!selected) {
      throw new Error(`workspace not found: ${workspace}`);
    }
    await sleep(100);
    debugLog('workspace-selection', 'end', { workspace });
  } catch (error) {
    debugLog('workspace-selection', 'failure', {
      workspace,
      error: error instanceof Error ? error.message : String(error)
    });
    throw error;
  } finally {
    clearInterval(snapshots);
  }
}

async function runCapture(args: CaptureArgs): Promise<void> {
  const debugLog = createDebugLog(args.debug);
  let currentPhase = 'startup';
  const watchdog = setTimeout(() => {
    debugLog(currentPhase, 'timeout', { captureTimeoutMs: args.captureTimeoutMs });
    console.error(`capture timed out after ${args.captureTimeoutMs}ms`);
    app.exit(124);
  }, args.captureTimeoutMs);
  watchdog.unref();
  await mkdir(args.outDir, { recursive: true });
  debugLog('electron-app', 'ready');
  const animusSettingsFile = animusSettingsPath(app.getPath('userData'));
  const mapCache = new MapCacheManager(app.getPath('userData'), 'tiles', async () => (await readAnimusSettings(animusSettingsFile)).activeMapCacheSetId);
  const demCache = new MapCacheManager(app.getPath('userData'), 'dem', async () => (await readAnimusSettings(animusSettingsFile)).activeDemCacheSetId);
  protocol.handle(ANIMUS_MAP_CACHE_PROTOCOL, async (request) => {
    const url = new URL(request.url);
    const [setId, z, x, y] = url.pathname.split('/').filter(Boolean);
    const manager = url.hostname === 'dem' ? demCache : mapCache;
    const tile = setId && z && x && y ? await manager.tilePath(decodeURIComponent(setId), z, x, y) : { path: '', found: false };
    return tile.found
      ? net.fetch(pathToFileURL(tile.path).href)
      : new Response(mapCache.emptyTileBytes(), { headers: { 'content-type': 'image/png' } });
  });
  ipcMain.handle('map-cache:status', async () => mapCache.status());
  ipcMain.handle('dem-cache:status', async () => demCache.status());
  const window = new BrowserWindow({
    width: args.viewports[0]?.width ?? 1440,
    height: args.viewports[0]?.height ?? 900,
    show: false,
    backgroundColor: '#0b1116',
    webPreferences: {
      preload: path.join(__dirname, 'electron-capture-preload.cjs'),
      contextIsolation: true,
      nodeIntegration: false,
      backgroundThrottling: false
    }
  });
  window.webContents.setZoomFactor(1);
  window.showInactive();
  debugLog('browser-window', 'created', {
    width: args.viewports[0]?.width ?? 1440,
    height: args.viewports[0]?.height ?? 900
  });
  wireDebugDiagnostics(window, debugLog);

  currentPhase = 'loadURL';
  debugLog('loadURL', 'start', { url: args.url });
  const loadSnapshots = startDebugSnapshots(window, debugLog, 'loadURL');
  try {
    await window.loadURL(args.url);
    debugLog('loadURL', 'end', { url: args.url });
  } catch (error) {
    debugLog('loadURL', 'failure', {
      url: args.url,
      error: error instanceof Error ? error.message : String(error)
    });
    throw error;
  } finally {
    clearInterval(loadSnapshots);
  }
  currentPhase = 'readiness-polling';
  const liveTelemetry = await waitForApp(window, args.waitTimeoutMs, debugLog);
  const captures: CaptureResult[] = [];

  for (const viewport of args.viewports) {
    currentPhase = 'viewport-resize';
    debugLog('viewport-resize', 'start', viewport);
    window.webContents.setZoomFactor(1);
    window.setContentSize(viewport.width, viewport.height);
    await sleep(args.settleMs);
    debugLog('viewport-resize', 'end', viewport);
    if (args.fullscreen && !window.isFullScreen()) {
      currentPhase = 'enter-full-screen';
      debugLog('enter-full-screen', 'start', viewport);
      const entered = waitForWindowEvent(window, 'enter-full-screen', Math.max(3000, args.settleMs * 4));
      window.setFullScreen(true);
      await entered;
      await sleep(args.settleMs);
      window.webContents.setZoomFactor(1);
      debugLog('enter-full-screen', 'end', await rendererSnapshot(window));
    }
    for (const workspace of args.workspaces) {
      currentPhase = `workspace-selection:${workspace}`;
      await selectWorkspace(window, workspace, debugLog);
      await window.webContents.executeJavaScript('window.__animusRefreshLayout?.("electron-capture");');
      window.webContents.sendInputEvent({ type: 'mouseMove', x: Math.max(0, viewport.width - 20), y: Math.max(0, viewport.height - 20) });
      await sleep(args.settleMs);
      const captureLabel = args.fullscreen ? `fullscreen-${viewport.label}` : viewport.label;
      const fileName = `${captureLabel}-${workspace}.png`;
      const filePath = path.join(args.outDir, fileName);
      currentPhase = `capturePage:${workspace}:${captureLabel}`;
      debugLog('capturePage', 'start', { workspace, viewport: captureLabel, path: filePath });
      const snapshots = startDebugSnapshots(window, debugLog, 'capturePage');
      let diagnostics: Record<string, unknown> = {};
      try {
        diagnostics = await rendererSnapshot(window);
        if (args.fullscreen) assertFullscreenLayout(diagnostics);
        const image = await window.webContents.capturePage();
        await writeFile(filePath, image.toPNG());
        debugLog('capturePage', 'end', { workspace, viewport: captureLabel, path: filePath, diagnostics });
      } catch (error) {
        debugLog('capturePage', 'failure', {
          workspace,
          viewport: captureLabel,
          path: filePath,
          error: error instanceof Error ? error.message : String(error)
        });
        throw error;
      } finally {
        clearInterval(snapshots);
      }
      captures.push({ workspace, viewport: captureLabel, path: filePath, liveTelemetry, diagnostics });
      console.log(`capture=${filePath}`);
    }
    if (args.fullscreen && window.isFullScreen()) {
      window.setFullScreen(false);
      await sleep(args.settleMs);
    }
  }

  const manifest = {
    url: args.url,
    capturedAt: new Date().toISOString(),
    liveTelemetry,
    captures
  };
  const manifestPath = path.join(args.outDir, 'manifest.json');
  currentPhase = 'manifest-write';
  debugLog('manifest-write', 'start', { path: manifestPath });
  await writeFile(manifestPath, `${JSON.stringify(manifest, null, 2)}\n`, 'utf8');
  debugLog('manifest-write', 'end', { path: manifestPath });
  console.log(`manifest=${manifestPath}`);
  console.log(`liveTelemetry=${liveTelemetry}`);
  clearTimeout(watchdog);
  currentPhase = 'app-quit';
  debugLog('app-quit', 'start');
}

const args = parseArgs(process.argv.slice(2));

app.disableHardwareAcceleration();
app.commandLine.appendSwitch('ignore-gpu-blocklist');
app.commandLine.appendSwitch('enable-unsafe-swiftshader');
app.commandLine.appendSwitch('use-angle', 'swiftshader');

app.whenReady()
  .then(() => runCapture(args))
  .then(() => app.quit())
  .catch((error) => {
    console.error(error);
    app.exit(1);
  });
