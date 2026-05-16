import dgram from 'node:dgram';
import path from 'node:path';
import { fileURLToPath } from 'node:url';
import { _electron as electron, type ElectronApplication, type Page } from 'playwright';
import { mavlinkV1Frame } from './mavlink.js';
import type { MavlinkServiceConfig } from './mavlink.js';

const __dirname = path.dirname(fileURLToPath(import.meta.url));
const appRoot = path.resolve(__dirname, '..');

declare global {
  interface Window {
    altairAnimus?: {
      getConfig: () => Promise<MavlinkServiceConfig>;
    };
  }
}

function testPort(): number {
  const fromEnv = Number(process.env.ALTAIR_TEST_MAVLINK_PORT ?? 0);
  if (fromEnv > 0 && fromEnv <= 65535) return fromEnv;
  return 30_000 + (process.pid % 20_000);
}

function sendUdp(packet: Buffer, port: number): Promise<void> {
  const socket = dgram.createSocket('udp4');
  return new Promise((resolve, reject) => {
    socket.send(packet, port, '127.0.0.1', (error) => {
      socket.close();
      if (error) reject(error);
      else resolve();
    });
  });
}

async function waitForApi(page: Page): Promise<number> {
  await page.waitForFunction(() => typeof window.altairAnimus?.getConfig === 'function', undefined, { timeout: 10_000 });
  const config = await page.evaluate(() => window.altairAnimus!.getConfig());
  return config.listenPort;
}

async function closeApp(app: ElectronApplication | null): Promise<void> {
  if (!app) return;
  try {
    await app.close();
  } catch {
    await app.evaluate(({ app: electronApp }) => electronApp.exit(1));
  }
}

async function main(): Promise<void> {
  const requestedPort = testPort();
  let app: ElectronApplication | null = null;
  try {
    app = await electron.launch({
      args: [
        appRoot,
        '--listen-host',
        '127.0.0.1',
        '--listen-port',
        String(requestedPort),
        '--no-qgc'
      ],
      cwd: appRoot
    });
    const window = await app.firstWindow();
    const listenPort = await waitForApi(window);
    const heartbeatPayload = Buffer.from([0, 0, 0, 0, 1, 0, 0, 4, 3]);
    await sendUdp(mavlinkV1Frame(0, heartbeatPayload, 2), listenPort);
    await window.waitForFunction(() => {
      const status = document.querySelector<HTMLElement>('#status')?.textContent ?? '';
      return status !== 'Disconnected' && status.includes('Connected');
    }, undefined, { timeout: 10_000 });
    const status = await window.locator('#status').textContent();
    if (!status || status === 'Disconnected') {
      throw new Error(`unexpected renderer status ${JSON.stringify(status)}`);
    }
    console.log(JSON.stringify({ ok: true, status, listenPort }));
  } finally {
    await closeApp(app);
  }
}

main().catch((error) => {
  console.error(error);
  process.exitCode = 1;
});
