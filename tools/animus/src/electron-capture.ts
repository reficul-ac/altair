import { app, BrowserWindow } from 'electron';
import { mkdir, writeFile } from 'node:fs/promises';
import path from 'node:path';

type CaptureArgs = {
  url: string;
  outDir: string;
  workspaces: string[];
  viewports: { width: number; height: number; label: string }[];
  waitTimeoutMs: number;
  settleMs: number;
};

type CaptureResult = {
  workspace: string;
  viewport: string;
  path: string;
  liveTelemetry: boolean;
};

function parseArgs(argv: string[]): CaptureArgs {
  const args: CaptureArgs = {
    url: 'http://127.0.0.1:5173',
    outDir: path.resolve('artifacts', 'animus-screenshots'),
    workspaces: ['flight', 'map', 'inspector'],
    viewports: [{ width: 1440, height: 900, label: '1440x900' }],
    waitTimeoutMs: 15000,
    settleMs: 800
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
    }
  }

  if (args.workspaces.length === 0) {
    throw new Error('at least one workspace is required');
  }
  if (args.viewports.length === 0) {
    throw new Error('at least one viewport is required');
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

async function waitForApp(window: BrowserWindow, timeoutMs: number): Promise<boolean> {
  const deadline = Date.now() + timeoutMs;
  let sawLiveTelemetry = false;
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
      return true;
    }
    await sleep(250);
  }
  return sawLiveTelemetry;
}

async function selectWorkspace(window: BrowserWindow, workspace: string): Promise<void> {
  const workspaceName = JSON.stringify(workspace);
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
  await window.webContents.executeJavaScript(`
    new Promise((resolve) => {
      requestAnimationFrame(() => requestAnimationFrame(() => {
        resolve(document.querySelector('[data-workspace].active')?.dataset.workspace || '');
      }));
    })
  `);
}

async function runCapture(args: CaptureArgs): Promise<void> {
  await mkdir(args.outDir, { recursive: true });
  const window = new BrowserWindow({
    width: args.viewports[0]?.width ?? 1440,
    height: args.viewports[0]?.height ?? 900,
    show: false,
    backgroundColor: '#0b1116',
    webPreferences: {
      contextIsolation: true,
      nodeIntegration: false
    }
  });

  await window.loadURL(args.url);
  const liveTelemetry = await waitForApp(window, args.waitTimeoutMs);
  const captures: CaptureResult[] = [];

  for (const viewport of args.viewports) {
    window.setSize(viewport.width, viewport.height);
    await sleep(args.settleMs);
    for (const workspace of args.workspaces) {
      await selectWorkspace(window, workspace);
      window.webContents.sendInputEvent({ type: 'mouseMove', x: Math.max(0, viewport.width - 20), y: Math.max(0, viewport.height - 20) });
      await sleep(args.settleMs);
      const fileName = `${viewport.label}-${workspace}.png`;
      const filePath = path.join(args.outDir, fileName);
      const image = await window.webContents.capturePage();
      await writeFile(filePath, image.toPNG());
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
  await writeFile(manifestPath, `${JSON.stringify(manifest, null, 2)}\n`, 'utf8');
  console.log(`manifest=${manifestPath}`);
  console.log(`liveTelemetry=${liveTelemetry}`);
}

const args = parseArgs(process.argv.slice(2));

app.commandLine.appendSwitch('ignore-gpu-blocklist');
app.commandLine.appendSwitch('enable-unsafe-swiftshader');
app.commandLine.appendSwitch('use-gl', 'swiftshader');

app.whenReady()
  .then(() => runCapture(args))
  .then(() => app.quit())
  .catch((error) => {
    console.error(error);
    app.exit(1);
  });
