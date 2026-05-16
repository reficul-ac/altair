import { app, BrowserWindow, type Event, type WebContentsConsoleMessageEventParams } from 'electron';
import { mkdir, writeFile } from 'node:fs/promises';
import path from 'node:path';

type CaptureArgs = {
  url: string;
  outDir: string;
  workspaces: string[];
  viewports: { width: number; height: number; label: string }[];
  waitTimeoutMs: number;
  settleMs: number;
  captureTimeoutMs: number;
  debug: boolean;
};

type CaptureResult = {
  workspace: string;
  viewport: string;
  path: string;
  liveTelemetry: boolean;
};

type DebugLog = ((phase: string, event: string, fields?: Record<string, unknown>) => void) & {
  enabled: boolean;
};

function parseArgs(argv: string[]): CaptureArgs {
  const args: CaptureArgs = {
    url: 'http://127.0.0.1:5173',
    outDir: path.resolve('artifacts', 'animus-screenshots'),
    workspaces: ['flight', 'map', 'inspector'],
    viewports: [{ width: 1440, height: 900, label: '1440x900' }],
    waitTimeoutMs: 15000,
    settleMs: 800,
    captureTimeoutMs: 60000,
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
        const activeWorkspace = document.querySelector('[data-workspace].active')?.dataset.workspace || null;
        const visiblePanel = document.querySelector('.workspace-visible')?.getAttribute('data-panel') || null;
        const canvases = [...document.querySelectorAll('canvas')].map((canvas) => ({
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
          readyState: document.readyState,
          title: document.title,
          shell: Boolean(document.querySelector('.shell')),
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
    focused: window.isFocused(),
    visible: window.isVisible()
  };
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
  const window = new BrowserWindow({
    width: args.viewports[0]?.width ?? 1440,
    height: args.viewports[0]?.height ?? 900,
    show: false,
    backgroundColor: '#0b1116',
    webPreferences: {
      contextIsolation: true,
      nodeIntegration: false,
      backgroundThrottling: false
    }
  });
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
    window.setSize(viewport.width, viewport.height);
    await sleep(args.settleMs);
    debugLog('viewport-resize', 'end', viewport);
    for (const workspace of args.workspaces) {
      currentPhase = `workspace-selection:${workspace}`;
      await selectWorkspace(window, workspace, debugLog);
      window.webContents.sendInputEvent({ type: 'mouseMove', x: Math.max(0, viewport.width - 20), y: Math.max(0, viewport.height - 20) });
      await sleep(args.settleMs);
      const fileName = `${viewport.label}-${workspace}.png`;
      const filePath = path.join(args.outDir, fileName);
      currentPhase = `capturePage:${workspace}:${viewport.label}`;
      debugLog('capturePage', 'start', { workspace, viewport: viewport.label, path: filePath });
      const snapshots = startDebugSnapshots(window, debugLog, 'capturePage');
      try {
        const image = await window.webContents.capturePage();
        await writeFile(filePath, image.toPNG());
        debugLog('capturePage', 'end', { workspace, viewport: viewport.label, path: filePath });
      } catch (error) {
        debugLog('capturePage', 'failure', {
          workspace,
          viewport: viewport.label,
          path: filePath,
          error: error instanceof Error ? error.message : String(error)
        });
        throw error;
      } finally {
        clearInterval(snapshots);
      }
      captures.push({ workspace, viewport: viewport.label, path: filePath, liveTelemetry });
      console.log(`capture=${filePath}`);
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
