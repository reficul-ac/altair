import { bindMapControls, drawMap } from './map-panel';
import { updateInspector } from './inspector-ui';
import { setHudMode, updateHud, updateStatusStrip, updateVehicleList, type HudMode } from './hud-ui';
import { SceneRenderer, nextCameraMode, type CameraMode, type ThemeName } from './scene-renderer';
import { parseSessionSnapshot, parseVehicleState, type SessionSnapshotMessage, type VehicleStateMessage } from './state';
import './styles.css';

type VisualizerConfig = {
  listenHost: string;
  listenPort: number;
  qgcForwarding: boolean;
  qgcEndpoints: { host: string; port: number }[];
};

type VisualizerApi = {
  onVehicleState: (callback: (message: VehicleStateMessage) => void) => () => void;
  onSessionSnapshot?: (callback: (message: SessionSnapshotMessage) => void) => () => void;
  onConfig: (callback: (config: VisualizerConfig) => void) => () => void;
  getConfig: () => Promise<VisualizerConfig>;
  setQgcForwarding: (enabled: boolean) => Promise<VisualizerConfig>;
  setListenPort: (port: number) => Promise<VisualizerConfig>;
  selectVehicle?: (id: string) => Promise<SessionSnapshotMessage>;
  addMarker?: (label: string) => Promise<SessionSnapshotMessage>;
};

declare global {
  interface Window {
    altairVisualizer?: VisualizerApi;
  }
}

const root = document.querySelector<HTMLDivElement>('#app');
if (!root) throw new Error('missing #app');

root.innerHTML = `
  <main class="shell theme-grid">
    <nav class="workspace-tabs" aria-label="Workspace">
      <button class="active" data-workspace="flight" type="button">Flight</button>
      <button data-workspace="map" type="button">Map</button>
      <button data-workspace="inspector" type="button">Inspector</button>
      <button data-workspace="session" type="button">Session</button>
    </nav>
    <section class="viewport workspace-panel active" data-panel="flight">
      <canvas id="scene"></canvas>
      <div class="status-strip" id="status-strip"></div>
      <div class="status-detail hidden" id="status-detail"></div>
      <div class="topbar">
        <div class="segmented" aria-label="Camera mode">
          <button class="active" data-camera="chase" type="button">Chase</button>
          <button data-camera="orbit" type="button">Orbit</button>
          <button data-camera="top" type="button">Top</button>
          <button data-camera="side" type="button">Side</button>
          <button data-camera="free" type="button">Free</button>
        </div>
        <div class="segmented" aria-label="HUD mode">
          <button class="active" data-hud="console" type="button">Console</button>
          <button data-hud="tactical" type="button">Tactical</button>
          <button data-hud="off" type="button">Off</button>
        </div>
        <button id="ortho-toggle" type="button">Ortho</button>
        <button id="debug-toggle" type="button">Debug</button>
        <button id="theme-toggle" type="button">Grid</button>
        <button id="marker" type="button">Marker</button>
      </div>
      <div class="status" id="status">Disconnected</div>
      <div class="hud hud-console" id="hud-console">
        <div class="attitude" aria-hidden="true">
          <div class="attitude-sky"></div>
          <div class="attitude-ground"></div>
          <div class="attitude-bank" id="attitude-bank"></div>
          <div class="attitude-cross"></div>
        </div>
        <div class="heading-tape" id="heading-tape"></div>
        <div class="telemetry-row">
          <div><span id="heading-label">HDG</span><strong id="heading">--</strong></div>
          <div><span>ROLL</span><strong id="roll">--</strong></div>
          <div><span>PITCH</span><strong id="pitch">--</strong></div>
          <div><span>ALT</span><strong id="altitude">--</strong></div>
          <div><span>GS</span><strong id="groundspeed">--</strong></div>
          <div><span>AS</span><strong id="airspeed">--</strong></div>
          <div><span>VS</span><strong id="climb">--</strong></div>
        </div>
      </div>
      <div class="hud hud-tactical hidden" id="hud-tactical">
        <div class="tactical-tag left"><span>GS</span><strong id="tactical-gs">--</strong></div>
        <div class="tactical-tag right"><span>ALT</span><strong id="tactical-alt">--</strong></div>
        <div class="gimbal-ring roll"></div>
        <div class="gimbal-ring pitch"></div>
        <div class="gimbal-ring yaw"></div>
        <canvas id="radar" width="180" height="180"></canvas>
      </div>
      <canvas class="ortho hidden" id="ortho" width="220" height="220"></canvas>
      <div class="debug hidden" id="debug">
        <h2>Debug</h2>
        <dl>
          <div><dt>FPS</dt><dd id="debug-fps">--</dd></div>
          <div><dt>Frame</dt><dd id="debug-frame">--</dd></div>
          <div><dt>Trail</dt><dd id="debug-trail">--</dd></div>
          <div><dt>Camera</dt><dd id="debug-camera">--</dd></div>
          <div><dt>Position</dt><dd id="debug-position">--</dd></div>
        </dl>
      </div>
      <div class="axis axis-east">E</div>
      <div class="axis axis-north">N</div>
    </section>
    <section class="workspace-panel map-workspace" data-panel="map">
      <div class="map-toolbar" aria-label="Map controls">
        <button id="map-focus" type="button">Focus</button>
        <button id="map-zoom-in" type="button">+</button>
        <button id="map-zoom-out" type="button">-</button>
      </div>
      <canvas id="map-canvas" width="1200" height="760"></canvas>
    </section>
    <section class="workspace-panel inspector-workspace" data-panel="inspector">
      <div class="inspector-grid">
        <section><h2>Messages</h2><input id="inspector-filter" type="search" placeholder="Filter messages" /><div class="inspector-header"><span>Name</span><span>Src</span><span>Rate</span><span>Count</span></div><div id="inspector-table"></div></section>
        <section><h2>Fields</h2><dl id="message-detail"></dl><div class="inspector-actions"><button id="inspector-export" type="button">Export CSV</button></div><div id="chart-fields" class="chart-fields"></div><canvas id="field-chart" width="520" height="180"></canvas></section>
      </div>
    </section>
    <aside class="metrics workspace-panel active" data-panel="session">
      <h1>Altair Live Debugger</h1>
      <section class="controls" aria-label="MAVLink controls">
        <label><span>MAVLink port</span><input id="listen-port" type="number" min="1" max="65535" value="14551" /></label>
        <label class="toggle"><input id="qgc" type="checkbox" checked /><span>Forward QGC</span></label>
        <p id="config">UDP 127.0.0.1:14551</p>
      </section>
      <div class="command-strip">
        <button id="pause" type="button">Pause</button>
        <button id="clear" type="button">Clear Trail</button>
        <button id="heading-mode" type="button">HDG</button>
      </div>
      <section class="vehicle-card">
        <span id="vehicle-type">Unknown vehicle</span>
        <strong id="vehicle-id">sys --</strong>
      </section>
      <section class="vehicle-switcher"><h2>Vehicles</h2><div id="vehicle-list"></div></section>
      <dl>
        <div><dt>Heartbeat</dt><dd id="heartbeat">--</dd></div>
        <div><dt>Packet Age</dt><dd id="packet">--</dd></div>
        <div><dt>Arming</dt><dd id="armed">--</dd></div>
        <div><dt>Mode</dt><dd id="mode">--</dd></div>
        <div><dt>GPS</dt><dd id="gps">--</dd></div>
        <div><dt>Battery</dt><dd id="battery">--</dd></div>
        <div><dt>Mission</dt><dd id="mission">--</dd></div>
        <div><dt>Throttle</dt><dd id="throttle">--</dd></div>
        <div><dt>Lat / Lon</dt><dd id="latlon">--</dd></div>
        <div><dt>Position</dt><dd id="position">--</dd></div>
        <div><dt>Velocity</dt><dd id="velocity">--</dd></div>
        <div><dt>Status Text</dt><dd id="statustext">--</dd></div>
      </dl>
      <section class="event-section"><h2>Events</h2><ul id="event-log"></ul></section>
    </aside>
  </main>
`;

const shell = document.querySelector<HTMLElement>('.shell')!;
const scene = new SceneRenderer(
  shell,
  document.querySelector<HTMLCanvasElement>('#scene')!,
  document.querySelector<HTMLCanvasElement>('#radar')!,
  document.querySelector<HTMLCanvasElement>('#ortho')!
);
const state = { hudMode: 'console' as HudMode, showYaw: false, snapshot: null as SessionSnapshotMessage | null, selected: null as VehicleStateMessage | null };
bindMapControls(() => state.snapshot);

function applyVehicle(message: VehicleStateMessage): void {
  state.selected = message;
  updateHud(message, state.showYaw);
  updateStatusStrip(message);
  scene.applyVehicle(message);
}

function applySnapshot(snapshot: SessionSnapshotMessage): void {
  state.snapshot = snapshot;
  const selected = snapshot.vehicles.find((vehicle) => vehicle.id === snapshot.selectedVehicleId) ?? snapshot.vehicles[0] ?? null;
  if (selected) applyVehicle(selected);
  scene.applyFleet(snapshot.vehicles, snapshot.selectedVehicleId, snapshot.events);
  updateVehicleList(snapshot, (id) => void window.altairVisualizer?.selectVehicle?.(id).then(applySnapshot));
  updateInspector(snapshot);
  drawMap(snapshot);
}

function updateConfig(config: VisualizerConfig): void {
  document.querySelector<HTMLInputElement>('#listen-port')!.value = String(config.listenPort);
  document.querySelector<HTMLInputElement>('#qgc')!.checked = config.qgcForwarding;
  const endpoints = config.qgcEndpoints.map((endpoint) => `${endpoint.host}:${endpoint.port}`).join(', ');
  document.querySelector<HTMLElement>('#config')!.textContent = `UDP ${config.listenHost}:${config.listenPort} / QGC ${config.qgcForwarding ? endpoints : 'off'}`;
}

function setWorkspace(name: string): void {
  document.querySelectorAll<HTMLButtonElement>('[data-workspace]').forEach((button) => button.classList.toggle('active', button.dataset.workspace === name));
  document.querySelectorAll<HTMLElement>('.workspace-panel').forEach((panel) => panel.classList.toggle('workspace-visible', panel.dataset.panel === name || panel.dataset.panel === 'session'));
  if (state.snapshot) drawMap(state.snapshot);
}

function connect(): void {
  if (window.altairVisualizer) {
    window.altairVisualizer.onVehicleState(applyVehicle);
    window.altairVisualizer.onSessionSnapshot?.(applySnapshot);
    window.altairVisualizer.onConfig(updateConfig);
    window.altairVisualizer.getConfig().then(updateConfig).catch(() => {
      document.querySelector<HTMLElement>('#status')!.textContent = 'MAVLink service unavailable';
    });
    document.querySelector<HTMLElement>('#status')!.textContent = 'Listening for MAVLink';
    return;
  }
  const wsUrl = new URLSearchParams(window.location.search).get('ws') ?? 'ws://127.0.0.1:8765';
  const socket = new WebSocket(wsUrl);
  socket.addEventListener('message', (event) => {
    const snapshot = parseSessionSnapshot(String(event.data));
    if (snapshot) applySnapshot(snapshot);
    const message = parseVehicleState(String(event.data));
    if (message) applyVehicle(message);
  });
  socket.addEventListener('close', () => setTimeout(connect, 1000));
}

document.querySelector<HTMLButtonElement>('#pause')!.addEventListener('click', (event) => {
  scene.paused = !scene.paused;
  (event.currentTarget as HTMLButtonElement).textContent = scene.paused ? 'Resume' : 'Pause';
});
document.querySelector<HTMLButtonElement>('#clear')!.addEventListener('click', () => scene.clearTrail());
document.querySelector<HTMLButtonElement>('#heading-mode')!.addEventListener('click', (event) => {
  state.showYaw = !state.showYaw;
  (event.currentTarget as HTMLButtonElement).textContent = state.showYaw ? 'YAW' : 'HDG';
  if (state.selected) updateHud(state.selected, state.showYaw);
});
document.querySelector<HTMLButtonElement>('#ortho-toggle')!.addEventListener('click', () => {
  scene.ortho = !scene.ortho;
  document.querySelector<HTMLCanvasElement>('#ortho')!.classList.toggle('hidden', !scene.ortho);
});
document.querySelector<HTMLButtonElement>('#debug-toggle')!.addEventListener('click', () => {
  scene.debug = !scene.debug;
  document.querySelector<HTMLElement>('#debug')!.classList.toggle('hidden', !scene.debug);
});
document.querySelector<HTMLButtonElement>('#theme-toggle')!.addEventListener('click', () => {
  const next: Record<ThemeName, ThemeName> = { grid: 'rez', rez: 'snow', snow: 'grid' };
  scene.setTheme(next[scene.theme]);
});
document.querySelector<HTMLButtonElement>('#marker')!.addEventListener('click', () => void window.altairVisualizer?.addMarker?.('Manual marker').then(applySnapshot));
document.querySelectorAll<HTMLButtonElement>('[data-camera]').forEach((button) => button.addEventListener('click', () => scene.setCameraMode(button.dataset.camera as CameraMode)));
document.querySelectorAll<HTMLButtonElement>('[data-hud]').forEach((button) => button.addEventListener('click', () => {
  state.hudMode = button.dataset.hud as HudMode;
  setHudMode(state.hudMode);
}));
document.querySelectorAll<HTMLButtonElement>('[data-workspace]').forEach((button) => button.addEventListener('click', () => setWorkspace(button.dataset.workspace ?? 'flight')));
document.querySelector<HTMLElement>('#status-strip')!.addEventListener('click', (event) => {
  const button = (event.target as HTMLElement).closest<HTMLButtonElement>('[data-detail]');
  if (!button) return;
  const detail = document.querySelector<HTMLElement>('#status-detail')!;
  const selected = state.selected;
  const detailMap: Record<string, string> = {
    link: selected?.connected ? 'MAVLink packets are arriving for the selected vehicle.' : 'No recent packets for the selected vehicle.',
    'packet-age': `Last packet age: ${selected?.packetAgeS?.toFixed(2) ?? '--'} s`,
    'heartbeat-age': `Last heartbeat age: ${selected?.heartbeatAgeS?.toFixed(2) ?? '--'} s`,
    'vehicle-kind': `Vehicle: ${selected?.vehicleType ?? '--'} / sys ${selected?.systemId ?? '--'} comp ${selected?.componentId ?? '--'}`,
    'arm-state': `Arming state: ${selected?.status?.armed === null || selected?.status?.armed === undefined ? '--' : selected.status.armed ? 'armed' : 'disarmed'}`,
    'gps-state': `GPS: ${selected?.status?.gpsFix ?? '--'} / satellites ${selected?.status?.satellitesVisible ?? '--'}`,
    'battery-state': `Battery: ${selected?.status?.batteryVoltageV ?? '--'} V / ${selected?.status?.batteryRemainingPct ?? '--'}%`
  };
  detail.textContent = detailMap[button.dataset.detail ?? ''] ?? button.textContent ?? '';
  detail.classList.remove('hidden');
});
document.querySelector<HTMLInputElement>('#qgc')!.addEventListener('change', (event) => {
  void window.altairVisualizer?.setQgcForwarding((event.currentTarget as HTMLInputElement).checked).then(updateConfig);
});
document.querySelector<HTMLInputElement>('#listen-port')!.addEventListener('change', (event) => {
  void window.altairVisualizer?.setListenPort(Number((event.currentTarget as HTMLInputElement).value)).then(updateConfig);
});
window.addEventListener('keydown', (event) => {
  if (event.code === 'KeyC') scene.setCameraMode(nextCameraMode(scene.cameraMode));
  if (event.code === 'KeyH') document.querySelector<HTMLButtonElement>(`[data-hud="${state.hudMode === 'console' ? 'tactical' : state.hudMode === 'tactical' ? 'off' : 'console'}"]`)?.click();
  if (event.code === 'KeyO') document.querySelector<HTMLButtonElement>('#ortho-toggle')!.click();
  if (event.code === 'KeyY') document.querySelector<HTMLButtonElement>('#heading-mode')!.click();
  if (event.code === 'KeyV') document.querySelector<HTMLButtonElement>('#theme-toggle')!.click();
  if (event.code === 'KeyM') document.querySelector<HTMLButtonElement>('#marker')!.click();
});

setHudMode('console');
scene.setTheme('grid');
document.querySelector<HTMLCanvasElement>('#ortho')!.classList.toggle('hidden', !scene.ortho);
setWorkspace('flight');
connect();
scene.start();
