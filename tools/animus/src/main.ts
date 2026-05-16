import { bindMapControls, drawMap } from './map-panel';
import { clearInspectorLog, recordInspectorSnapshot, updateInspector } from './inspector-ui';
import { setHudMode, updateHud, updateStatusStrip, updateVehicleList, type HudMode } from './hud-ui';
import { SceneRenderer, nextCameraMode, type CameraMode, type ThemeName } from './scene-renderer';
import {
  createEmptyMissionPlan,
  validateMission,
  type CommandDispatchResult,
  type CommandName,
  type GuardedCommandRequest,
  type GuardedCommandResult,
  type MissionPlan,
  type MissionTransferState,
  type MissionValidationResult,
  type MockLinkState,
  parseSessionSnapshot,
  parseVehicleState,
  type ReplayTimelineMessage,
  type SessionSnapshotMessage,
  type VehicleStateMessage
} from './state';
import './styles.css';

type AnimusConfig = {
  listenHost: string;
  listenPort: number;
  qgcForwarding: boolean;
  qgcEndpoints: { host: string; port: number }[];
  writableAnimus: boolean;
};

type AnimusApi = {
  onVehicleState: (callback: (message: VehicleStateMessage) => void) => () => void;
  onSessionSnapshot?: (callback: (message: SessionSnapshotMessage) => void) => () => void;
  onConfig: (callback: (config: AnimusConfig) => void) => () => void;
  getConfig: () => Promise<AnimusConfig>;
  setQgcForwarding: (enabled: boolean) => Promise<AnimusConfig>;
  setListenPort: (port: number) => Promise<AnimusConfig>;
  selectVehicle?: (id: string) => Promise<SessionSnapshotMessage>;
  addMarker?: (label: string) => Promise<SessionSnapshotMessage>;
  validateMission?: (plan: MissionPlan) => Promise<MissionValidationResult>;
  saveMission?: (plan: MissionPlan) => Promise<{ saved: boolean; path?: string; reason?: string; validation: MissionValidationResult }>;
  loadMission?: () => Promise<{ loaded: boolean; path?: string; plan?: MissionPlan; validation?: MissionValidationResult }>;
  uploadMissionToSitl?: (plan: MissionPlan, vehicleId?: string) => Promise<MissionTransferState>;
  downloadMissionFromSitl?: (vehicleId?: string) => Promise<MissionTransferState>;
  onReplayState?: (callback: (message: ReplayTimelineMessage) => void) => () => void;
  openReplay?: () => Promise<ReplayTimelineMessage>;
  importLog?: () => Promise<ReplayTimelineMessage>;
  exportSessionLog?: () => Promise<{ saved: boolean; path?: string }>;
  startMockLink?: (vehicleCount: number) => Promise<MockLinkState>;
  issueCommand?: (request: GuardedCommandRequest) => Promise<GuardedCommandResult | CommandDispatchResult>;
  replayPlay?: () => Promise<ReplayTimelineMessage>;
  replayPause?: () => Promise<ReplayTimelineMessage>;
  replaySeek?: (timestampS: number) => Promise<ReplayTimelineMessage>;
  replaySetSpeed?: (speed: number) => Promise<ReplayTimelineMessage>;
  replayReset?: () => Promise<ReplayTimelineMessage>;
  replayMarker?: (direction: -1 | 1) => Promise<ReplayTimelineMessage>;
};

declare global {
  interface Window {
    altairAnimus?: AnimusApi;
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
      <button data-workspace="video" type="button">Video</button>
      <button data-workspace="plan" type="button">Plan</button>
      <button data-workspace="setup" type="button">Setup</button>
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
        <section><h2>Fields</h2><dl id="message-detail"></dl><div class="inspector-actions"><button id="inspector-log-export" type="button">Export Log</button><button id="inspector-export" type="button">Export CSV</button></div><div id="chart-fields" class="chart-fields"></div><canvas id="field-chart" width="520" height="180"></canvas></section>
      </div>
    </section>
    <section class="workspace-panel inspector-workspace" data-panel="video">
      <div class="analysis-grid">
        <section><h2>Camera Streams</h2><div id="camera-streams" class="tool-list"></div></section>
        <section><h2>Capture</h2><div class="command-grid"><button data-camera-action="snapshot" type="button" disabled title="Still capture requires real camera stream plumbing.">Capture unavailable</button><button data-camera-action="record" type="button" disabled title="Local recording requires real camera stream plumbing.">Record unavailable</button><button data-camera-action="subtitle" type="button" disabled title="Telemetry subtitle export requires protocol-backed stream support.">Subtitle export unavailable</button></div><p id="camera-detail" class="empty">Capture, recording, settings, and subtitle export are disabled until real camera stream support exists.</p></section>
      </div>
    </section>
    <section class="workspace-panel inspector-workspace" data-panel="plan">
      <div class="analysis-grid">
        <section><h2>Mission</h2><div id="mission-list" class="tool-list"></div><div id="mission-validation" class="tool-list"></div><div class="inspector-actions"><button id="plan-add" type="button">Add WP</button><button id="plan-save" type="button">Save</button><button id="plan-restore" type="button">Load</button><button id="plan-upload" type="button">Upload SITL</button></div></section>
        <section><h2>Geofence / Rally</h2><div id="fence-list" class="tool-list"></div><div class="command-grid"><button data-plan-tool="geofence" type="button" disabled title="Geofence editing is roadmap-only.">Fence view only</button><button data-plan-tool="rally" type="button" disabled title="Rally editing is roadmap-only.">Rally view only</button><button data-plan-tool="survey" type="button" disabled title="Survey planning is roadmap-only.">Survey unavailable</button><button data-plan-tool="corridor" type="button" disabled title="Corridor scan planning is roadmap-only.">Corridor unavailable</button><button data-plan-tool="structure" type="button" disabled title="Structure scan planning is roadmap-only.">Structure unavailable</button><button data-plan-tool="landing" type="button" disabled title="Fixed-wing landing pattern planning is roadmap-only.">Landing unavailable</button></div></section>
      </div>
    </section>
    <section class="workspace-panel inspector-workspace" data-panel="setup">
      <div class="analysis-grid">
        <section><h2>Readiness</h2><div id="readiness-list" class="tool-list"></div><div class="command-grid" id="guarded-commands"></div></section>
        <section><h2>Parameters / Diagnostics</h2><input id="parameter-filter" type="search" placeholder="Filter parameters" /><div id="parameter-list" class="tool-list"></div><div id="setup-placeholder-list" class="tool-list"><div><strong>Setup edits</strong><span>disabled</span><span>roadmap</span></div><div><strong>Calibration</strong><span>disabled</span><span>roadmap</span></div></div><div id="diagnostics-list" class="tool-list"></div></section>
      </div>
    </section>
    <aside class="metrics workspace-panel active" data-panel="session">
      <h1>Altair Live Debugger</h1>
      <section class="controls" aria-label="MAVLink controls">
        <label><span>MAVLink port</span><input id="listen-port" type="number" min="1" max="65535" value="14551" /></label>
        <label class="toggle"><input id="qgc" type="checkbox" checked /><span>Forward QGC</span></label>
        <p id="config">UDP 127.0.0.1:14551</p>
      </section>
      <section class="replay-controls" aria-label="Replay controls">
        <div class="replay-buttons">
          <button id="replay-open" type="button">Open</button>
          <button id="log-import" type="button">Import</button>
          <button id="log-download" type="button">Download</button>
          <button id="replay-play" type="button" disabled>Play</button>
          <button id="replay-reset" type="button">Reset</button>
        </div>
        <input id="replay-timeline" type="range" min="0" max="0.001" step="0.001" value="0" disabled />
        <div class="replay-secondary">
          <button id="replay-prev-marker" type="button">Prev</button>
          <select id="replay-speed" aria-label="Playback speed">
            <option value="0.25">0.25x</option>
            <option value="0.5">0.5x</option>
            <option value="1" selected>1x</option>
            <option value="2">2x</option>
            <option value="4">4x</option>
            <option value="8">8x</option>
          </select>
          <button id="replay-next-marker" type="button">Next</button>
        </div>
        <label class="toggle"><input id="sync-inspection" type="checkbox" checked /><span>Lock timestamp</span></label>
        <p id="replay-time">0:00 / 0:00 @ 1x</p>
        <p id="replay-meta">No replay loaded</p>
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
      <section class="vehicle-switcher"><h2>Compare</h2><div id="vehicle-comparison" class="vehicle-comparison"></div></section>
      <section class="vehicle-switcher"><h2>Analysis</h2><div id="analysis-summary" class="vehicle-comparison"></div></section>
      <section class="vehicle-switcher"><h2>Mock Link</h2><div class="replay-secondary"><input id="mock-count" type="number" min="1" max="32" value="3" /><button id="mock-start" type="button">Start</button></div><p id="mock-status">Offline maps and mock links idle</p></section>
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
const state = {
  hudMode: 'console' as HudMode,
  showYaw: false,
  snapshot: null as SessionSnapshotMessage | null,
  selected: null as VehicleStateMessage | null,
  mission: createEmptyMissionPlan(),
  replay: null as ReplayTimelineMessage | null,
  syncInspection: true
};
bindMapControls(() => state.snapshot);

function applyVehicle(message: VehicleStateMessage): void {
  state.selected = message;
  updateHud(message, state.showYaw);
  updateStatusStrip(message);
  scene.applyVehicle(message);
}

function applySnapshot(snapshot: SessionSnapshotMessage): void {
  state.snapshot = snapshot;
  recordInspectorSnapshot(snapshot, state.replay?.loaded ? state.replay.timestampS : performance.now() / 1000);
  const selected = snapshot.vehicles.find((vehicle) => vehicle.id === snapshot.selectedVehicleId) ?? snapshot.vehicles[0] ?? null;
  if (selected) applyVehicle(selected);
  scene.applyFleet(snapshot.vehicles, snapshot.selectedVehicleId, snapshot.events);
  updateVehicleList(snapshot, (id) => void window.altairAnimus?.selectVehicle?.(id).then(applySnapshot));
  updateInspector(snapshot);
  drawMap(snapshot);
  updateVehicleComparison(snapshot);
  updateAnalysis(snapshot);
  updateGcsSurfaces(snapshot);
}

function updateConfig(config: AnimusConfig): void {
  document.querySelector<HTMLInputElement>('#listen-port')!.value = String(config.listenPort);
  document.querySelector<HTMLInputElement>('#qgc')!.checked = config.qgcForwarding;
  const endpoints = config.qgcEndpoints.map((endpoint) => `${endpoint.host}:${endpoint.port}`).join(', ');
  document.querySelector<HTMLElement>('#config')!.textContent = `UDP ${config.listenHost}:${config.listenPort} / QGC ${config.qgcForwarding ? endpoints : 'off'} / ${config.writableAnimus ? 'SITL writable' : 'read-only'}`;
}

function setWorkspace(name: string): void {
  document.querySelectorAll<HTMLButtonElement>('[data-workspace]').forEach((button) => button.classList.toggle('active', button.dataset.workspace === name));
  document.querySelectorAll<HTMLElement>('.workspace-panel').forEach((panel) => panel.classList.toggle('workspace-visible', panel.dataset.panel === name || panel.dataset.panel === 'session'));
  if (state.snapshot) drawMap(state.snapshot);
}

function connect(): void {
  if (window.altairAnimus) {
    window.altairAnimus.onVehicleState(applyVehicle);
    window.altairAnimus.onSessionSnapshot?.(applySnapshot);
    window.altairAnimus.onReplayState?.(updateReplayControls);
    window.altairAnimus.onConfig(updateConfig);
    window.altairAnimus.getConfig().then(updateConfig).catch(() => {
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

function updateReplayControls(replay: ReplayTimelineMessage): void {
  state.replay = replay;
  const timeline = document.querySelector<HTMLInputElement>('#replay-timeline');
  const play = document.querySelector<HTMLButtonElement>('#replay-play');
  const time = document.querySelector<HTMLElement>('#replay-time');
  const meta = document.querySelector<HTMLElement>('#replay-meta');
  if (timeline) {
    timeline.max = String(Math.max(0.001, replay.durationS));
    timeline.value = String(replay.timestampS);
    timeline.disabled = !replay.loaded;
  }
  if (play) {
    play.textContent = replay.playing ? 'Pause' : 'Play';
    play.disabled = !replay.loaded;
  }
  if (time) {
    time.textContent = `${formatTime(replay.timestampS)} / ${formatTime(replay.durationS)} @ ${replay.speed}x`;
  }
  if (meta) {
    meta.textContent = replay.metadata
      ? `${replay.metadata.sourceType} / ${replay.metadata.frameCount} frames / ${replay.metadata.vehicleIds.join(', ') || 'no vehicles'}`
      : 'No replay loaded';
  }
}

function updateVehicleComparison(snapshot: SessionSnapshotMessage): void {
  const target = document.querySelector<HTMLElement>('#vehicle-comparison');
  if (!target) return;
  target.innerHTML = snapshot.vehicles.map((vehicle) => {
    const id = vehicle.id ?? `${vehicle.systemId}:${vehicle.componentId}`;
    const position = vehicle.localPosition.eastM === null || vehicle.localPosition.northM === null
      ? '--'
      : `${vehicle.localPosition.northM.toFixed(1)} N / ${vehicle.localPosition.eastM.toFixed(1)} E`;
    const armed = vehicle.status?.armed === null || vehicle.status?.armed === undefined ? '--' : vehicle.status.armed ? 'ARM' : 'SAFE';
    return `<div><strong>${escapeHtml(id)}</strong><span>${escapeHtml(vehicle.vehicleType ?? 'MAVLink')}</span><span>${escapeHtml(vehicle.status?.mode ?? '--')}</span><span>${armed}</span><span>${position}</span></div>`;
  }).join('') || '<p class="empty">No vehicle streams</p>';
}

function updateAnalysis(snapshot: SessionSnapshotMessage): void {
  const target = document.querySelector<HTMLElement>('#analysis-summary');
  if (!target) return;
  const analysis = snapshot.analysis;
  if (!analysis) {
    target.innerHTML = '<p class="empty">No synchronized analysis yet</p>';
    return;
  }
  target.innerHTML = `
    <div><strong>Target</strong><span>${analysis.targetVehicleCount} vehicles</span></div>
    <div><strong>Align</strong><span>${escapeHtml(analysis.alignment)}</span></div>
    <div><strong>Formation</strong><span>${analysis.formation.vehicles.length} tracked</span></div>
    <div><strong>Separation</strong><span>${analysis.deconfliction.minimumSeparationM?.toFixed(1) ?? '--'} m</span></div>
  `;
}

function updateGcsSurfaces(snapshot: SessionSnapshotMessage): void {
  const selected = snapshot.vehicles.find((vehicle) => vehicle.id === snapshot.selectedVehicleId) ?? snapshot.vehicles[0] ?? null;
  updateCameraStreams(selected);
  updatePlanSurface(selected);
  updateSetupSurface(selected);
  const mock = snapshot.mockLinks?.[0];
  if (mock) document.querySelector<HTMLElement>('#mock-status')!.textContent = `${mock.label} / ${mock.diagnostics.status}`;
}

function updateCameraStreams(vehicle: VehicleStateMessage | null): void {
  const streams = vehicle?.cameraStreams ?? [];
  document.querySelector<HTMLElement>('#camera-streams')!.innerHTML = streams.map((stream) => `
    <div><strong>${escapeHtml(stream.label)}</strong><span>${escapeHtml(stream.kind.toUpperCase())}</span><span>${escapeHtml(stream.status)} / inspect only</span></div>
  `).join('') || '<p class="empty">No RTP, RTSP, UVC, or MAVLink camera metadata streams advertised</p>';
}

function updatePlanSurface(vehicle: VehicleStateMessage | null): void {
  const validation = validateMission(state.mission);
  document.querySelector<HTMLElement>('#mission-list')!.innerHTML = state.mission.waypoints.map((item) => `
    <div class="mission-edit-row" data-waypoint="${item.seq}">
      <strong>#${item.seq}</strong>
      <label>Lat <input data-mission-field="lat_deg" data-waypoint="${item.seq}" type="number" step="0.000001" value="${item.lat_deg}" /></label>
      <label>Lon <input data-mission-field="lon_deg" data-waypoint="${item.seq}" type="number" step="0.000001" value="${item.lon_deg}" /></label>
      <label>Alt <input data-mission-field="alt_m" data-waypoint="${item.seq}" type="number" step="1" value="${item.alt_m}" /></label>
      <label>Thr <input data-mission-field="throttle" data-waypoint="${item.seq}" type="number" min="0" max="1" step="0.05" value="${item.throttle}" /></label>
      <label>Rad <input data-mission-field="acceptance_radius_m" data-waypoint="${item.seq}" type="number" min="0.1" step="1" value="${item.acceptance_radius_m}" /></label>
    </div>
  `).join('') || '<p class="empty">No local mission waypoints</p>';
  document.querySelector<HTMLElement>('#mission-validation')!.innerHTML = validation.issues
    .map((issue) => `<div><strong>${escapeHtml(issue.severity)}</strong><span>${escapeHtml(issue.path)}</span><span>${escapeHtml(issue.message)}</span></div>`)
    .join('') || `<div><strong>Valid</strong><span>${validation.waypointCount} waypoint${validation.waypointCount === 1 ? '' : 's'}</span><span>${vehicle?.commandCapabilities?.writableLink ? 'SITL writable' : 'read-only link'}</span></div>`;
  document.querySelectorAll<HTMLInputElement>('[data-mission-field]').forEach((input) => {
    input.addEventListener('change', () => {
      const seq = Number(input.dataset.waypoint);
      const field = input.dataset.missionField as keyof MissionPlan['waypoints'][number];
      const waypoint = state.mission.waypoints[seq];
      if (waypoint && field !== 'seq') {
        waypoint[field] = Number(input.value) as never;
        updatePlanSurface(state.selected);
      }
    });
  });
  const upload = document.querySelector<HTMLButtonElement>('#plan-upload');
  if (upload) upload.disabled = !validation.valid || !vehicle?.commandCapabilities?.writableLink;
  const fences = vehicle?.geofences ?? [];
  const rally = vehicle?.rallyPoints ?? [];
  document.querySelector<HTMLElement>('#fence-list')!.innerHTML = [
    ...fences.map((zone) => `<div><strong>${escapeHtml(zone.id)}</strong><span>${zone.kind}</span><span>${zone.inclusion ? 'include' : 'exclude'}</span></div>`),
    ...rally.map((point) => `<div><strong>${escapeHtml(point.id)}</strong><span>rally</span><span>${point.altitudeM?.toFixed(1) ?? '--'} m</span></div>`)
  ].join('') || '<p class="empty">No geofence or rally points</p>';
}

function updateSetupSurface(vehicle: VehicleStateMessage | null): void {
  const readiness = [
    ['Live link', vehicle?.connected ? 'ready' : 'missing'],
    ['GPS', vehicle?.status?.gpsFix ?? '--'],
    ['Battery', vehicle?.status?.batteryRemainingPct === null || vehicle?.status?.batteryRemainingPct === undefined ? '--' : `${vehicle.status.batteryRemainingPct}%`],
    ['Mission', vehicle?.mission?.state?.state ?? vehicle?.status?.missionSeq ?? '--']
  ];
  document.querySelector<HTMLElement>('#readiness-list')!.innerHTML = readiness.map(([label, value]) => `<div><strong>${escapeHtml(String(label))}</strong><span>${escapeHtml(String(value))}</span></div>`).join('');
  const parameters = vehicle?.parameters ?? [];
  const query = document.querySelector<HTMLInputElement>('#parameter-filter')?.value.toLowerCase() ?? '';
  document.querySelector<HTMLElement>('#parameter-list')!.innerHTML = parameters
    .filter((param) => param.name.toLowerCase().includes(query))
    .map((param) => `<div><strong>${escapeHtml(param.name)}</strong><span>${escapeHtml(String(param.value))}</span><span>${param.readonly ? 'read-only' : 'inspect only'}</span></div>`)
    .join('') || '<p class="empty">No parameters loaded</p>';
  const diag = vehicle?.diagnostics;
  document.querySelector<HTMLElement>('#diagnostics-list')!.innerHTML = diag
    ? `<div><strong>${escapeHtml(diag.linkId)}</strong><span>${escapeHtml(diag.transport)}</span><span>${escapeHtml(diag.status)}</span><span>${diag.packetsRx} rx</span></div>`
    : '<p class="empty">No link diagnostics</p>';
  renderGuardedCommands(vehicle);
}

function renderGuardedCommands(vehicle: VehicleStateMessage | null): void {
  const commands: CommandName[] = ['arm', 'disarm', 'emergency-stop', 'takeoff', 'land', 'return-to-launch', 'pause', 'change-altitude', 'go-to', 'orbit', 'mission-start', 'mission-continue', 'mission-resume'];
  const container = document.querySelector<HTMLElement>('#guarded-commands')!;
  const supported = vehicle?.commandCapabilities?.supported ?? [];
  container.innerHTML = commands.map((command) => `<button type="button" data-command="${command}" ${supported.includes(command) ? '' : 'disabled'}>${escapeHtml(command)}</button>`).join('');
  container.querySelectorAll<HTMLButtonElement>('[data-command]').forEach((button) => {
    button.addEventListener('click', () => {
      const command = button.dataset.command as CommandName;
      const vehicleId = vehicle?.id ?? `${vehicle?.systemId ?? '--'}:${vehicle?.componentId ?? '--'}`;
      if (!window.confirm(`Send ${command} to ${vehicleId}?`)) return;
      void window.altairAnimus?.issueCommand?.({ command, vehicleId, confirmed: true }).then((result) => {
        document.querySelector<HTMLElement>('#status')!.textContent = result.reason;
      });
    });
  });
}

function formatTime(seconds: number): string {
  const clamped = Math.max(0, seconds);
  const minutes = Math.floor(clamped / 60);
  const wholeSeconds = Math.floor(clamped % 60);
  return `${minutes}:${wholeSeconds.toString().padStart(2, '0')}`;
}

function escapeHtml(value: string): string {
  return value.replaceAll('&', '&amp;').replaceAll('<', '&lt;').replaceAll('>', '&gt;').replaceAll('"', '&quot;').replaceAll("'", '&#39;');
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
document.querySelector<HTMLButtonElement>('#marker')!.addEventListener('click', () => void window.altairAnimus?.addMarker?.('Manual marker').then(applySnapshot));
document.querySelector<HTMLButtonElement>('#replay-open')!.addEventListener('click', () => {
  clearInspectorLog();
  void window.altairAnimus?.openReplay?.().then(updateReplayControls);
});
document.querySelector<HTMLButtonElement>('#log-import')!.addEventListener('click', () => {
  clearInspectorLog();
  void window.altairAnimus?.importLog?.().then(updateReplayControls);
});
document.querySelector<HTMLButtonElement>('#log-download')!.addEventListener('click', () => {
  void window.altairAnimus?.exportSessionLog?.();
});
document.querySelector<HTMLButtonElement>('#replay-play')!.addEventListener('click', () => {
  const replay = state.replay;
  const command = replay?.playing ? window.altairAnimus?.replayPause : window.altairAnimus?.replayPlay;
  void command?.().then(updateReplayControls);
});
document.querySelector<HTMLButtonElement>('#replay-reset')!.addEventListener('click', () => void window.altairAnimus?.replayReset?.().then(updateReplayControls));
document.querySelector<HTMLInputElement>('#replay-timeline')!.addEventListener('input', (event) => {
  void window.altairAnimus?.replaySeek?.(Number((event.currentTarget as HTMLInputElement).value)).then(updateReplayControls);
});
document.querySelector<HTMLSelectElement>('#replay-speed')!.addEventListener('change', (event) => {
  void window.altairAnimus?.replaySetSpeed?.(Number((event.currentTarget as HTMLSelectElement).value)).then(updateReplayControls);
});
document.querySelector<HTMLButtonElement>('#replay-prev-marker')!.addEventListener('click', () => void window.altairAnimus?.replayMarker?.(-1).then(updateReplayControls));
document.querySelector<HTMLButtonElement>('#replay-next-marker')!.addEventListener('click', () => void window.altairAnimus?.replayMarker?.(1).then(updateReplayControls));
document.querySelector<HTMLInputElement>('#sync-inspection')!.addEventListener('change', (event) => {
  state.syncInspection = (event.currentTarget as HTMLInputElement).checked;
});
document.querySelector<HTMLButtonElement>('#mock-start')!.addEventListener('click', () => {
  const count = Number(document.querySelector<HTMLInputElement>('#mock-count')!.value);
  void window.altairAnimus?.startMockLink?.(count).then((mock) => {
    document.querySelector<HTMLElement>('#mock-status')!.textContent = `${mock.label} / ${mock.diagnostics.status}`;
  });
});
document.querySelector<HTMLButtonElement>('#plan-add')!.addEventListener('click', () => {
  const vehicle = state.selected;
  const seq = state.mission.waypoints.length;
  if (seq >= 16) return;
  state.mission.waypoints.push({
    seq,
    lat_deg: vehicle?.globalPosition.latDeg ?? 37.4275,
    lon_deg: vehicle?.globalPosition.lonDeg ?? -122.1697,
    alt_m: vehicle?.globalPosition.altitudeM ?? 120,
    throttle: 0.5,
    acceptance_radius_m: 25
  });
  updatePlanSurface(state.selected);
});
document.querySelector<HTMLButtonElement>('#plan-save')!.addEventListener('click', () => {
  void window.altairAnimus?.saveMission?.(state.mission).then((result) => {
    document.querySelector<HTMLElement>('#status')!.textContent = result.saved ? `Mission saved ${result.path ?? ''}` : result.reason ?? 'Mission save canceled';
  });
});
document.querySelector<HTMLButtonElement>('#plan-restore')!.addEventListener('click', () => {
  void window.altairAnimus?.loadMission?.().then((result) => {
    if (result.loaded && result.plan) {
      state.mission = result.plan;
      updatePlanSurface(state.selected);
    }
  });
});
document.querySelector<HTMLButtonElement>('#plan-upload')!.addEventListener('click', () => {
  const vehicleId = state.selected?.id ?? `${state.selected?.systemId ?? '--'}:${state.selected?.componentId ?? '--'}`;
  if (!window.confirm(`Upload ${state.mission.waypoints.length} waypoint mission to ${vehicleId}?`)) return;
  void window.altairAnimus?.uploadMissionToSitl?.(state.mission, vehicleId).then((result) => {
    document.querySelector<HTMLElement>('#status')!.textContent = result.reason;
    updatePlanSurface(state.selected);
  });
});
document.querySelector<HTMLInputElement>('#parameter-filter')!.addEventListener('input', () => {
  if (state.snapshot) updateSetupSurface(state.selected);
});
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
  void window.altairAnimus?.setQgcForwarding((event.currentTarget as HTMLInputElement).checked).then(updateConfig);
});
document.querySelector<HTMLInputElement>('#listen-port')!.addEventListener('change', (event) => {
  void window.altairAnimus?.setListenPort(Number((event.currentTarget as HTMLInputElement).value)).then(updateConfig);
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
