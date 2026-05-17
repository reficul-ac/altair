import { bindMapControls, drawMap } from './map-panel';
import { clearInspectorLog, recordInspectorSnapshot, updateInspector } from './inspector-ui';
import { setHudMode, updateHud, updateStatusStrip, updateVehicleList, type HudMode } from './hud-ui';
import { SceneRenderer, nextCameraMode, type CameraMode, type ThemeName } from './scene-renderer';
import { createDashboardController } from './dashboard-ui';
import type { AnimusDashboardLayout } from './dashboard-types';
import {
  createEmptyMissionPlan,
  validateMission,
  type CommandAuditEntry,
  type CommandDispatchResult,
  type CommandName,
  type CommandTransaction,
  type GuardedCommandRequest,
  type GuardedCommandResult,
  type MissionPlan,
  type MissionTransferState,
  type MissionValidationResult,
  type MockLinkState,
  type ParameterEditRequest,
  type ParameterEditResult,
  parseSessionSnapshot,
  parseVehicleState,
  type ReplayTimelineMessage,
  type SessionSnapshotMessage,
  type VehicleStateMessage
} from './state';
import { renderAppShell } from './app-shell';
import './styles.css';

type AnimusConfig = {
  listenHost: string;
  listenPort: number;
  qgcForwarding: boolean;
  qgcEndpoints: { host: string; port: number }[];
  writableAnimus: boolean;
  authorityMode: string;
};

type AnimusWorkspaceName = 'flight' | 'dashboard' | 'map' | 'inspector' | 'video' | 'plan' | 'setup';

type AnimusUiSettings = {
  schemaVersion: 1;
  defaultWorkspace: AnimusWorkspaceName;
  theme: ThemeName;
  cameraMode: CameraMode;
  cameraLock: boolean;
  lastDashboardPresetLabel: string | null;
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
  getSettings?: () => Promise<AnimusUiSettings>;
  saveSettings?: (settings: AnimusUiSettings) => Promise<AnimusUiSettings>;
  getDashboardLayout?: () => Promise<AnimusDashboardLayout>;
  saveDashboardLayout?: (layout: AnimusDashboardLayout) => Promise<AnimusDashboardLayout>;
  resetDashboardLayout?: () => Promise<AnimusDashboardLayout>;
  exportDashboardProfile?: (layout: AnimusDashboardLayout) => Promise<{ saved: boolean; path?: string }>;
  importDashboardProfile?: () => Promise<{ imported: boolean; path?: string; layout?: AnimusDashboardLayout }>;
  validateMission?: (plan: MissionPlan) => Promise<MissionValidationResult>;
  saveMission?: (plan: MissionPlan) => Promise<{ saved: boolean; path?: string; reason?: string; validation: MissionValidationResult }>;
  loadMission?: () => Promise<{ loaded: boolean; path?: string; plan?: MissionPlan; validation?: MissionValidationResult }>;
  uploadMissionToSitl?: (plan: MissionPlan, vehicleId?: string) => Promise<MissionTransferState>;
  downloadMissionFromSitl?: (vehicleId?: string) => Promise<MissionTransferState>;
  refreshParameters?: (vehicleId?: string) => Promise<ParameterEditResult>;
  setParameter?: (request: ParameterEditRequest) => Promise<ParameterEditResult>;
  uploadMission?: (plan: MissionPlan, vehicleId?: string, confirmed?: boolean) => Promise<MissionTransferState>;
  downloadMission?: (vehicleId?: string) => Promise<MissionTransferState>;
  clearMission?: (vehicleId?: string, confirmed?: boolean) => Promise<MissionTransferState>;
  listOnboardLogs?: (vehicleId?: string) => Promise<ParameterEditResult>;
  downloadOnboardLog?: (logId: number, vehicleId?: string) => Promise<ParameterEditResult>;
  eraseOnboardLogs?: (vehicleId?: string, confirmed?: boolean) => Promise<ParameterEditResult>;
  requestTerrain?: (vehicleId?: string) => Promise<ParameterEditResult>;
  cameraCapture?: (vehicleId?: string) => Promise<ParameterEditResult>;
  cameraRecord?: (recording: boolean, vehicleId?: string) => Promise<ParameterEditResult>;
  cameraSetSetting?: (setting: 'zoom' | 'focus', value: number, vehicleId?: string) => Promise<ParameterEditResult>;
  onReplayState?: (callback: (message: ReplayTimelineMessage) => void) => () => void;
  openReplay?: () => Promise<ReplayTimelineMessage>;
  importLog?: () => Promise<ReplayTimelineMessage>;
  exportSessionLog?: () => Promise<{ saved: boolean; path?: string }>;
  startMockLink?: (vehicleCount: number) => Promise<MockLinkState>;
  issueCommand?: (request: GuardedCommandRequest) => Promise<GuardedCommandResult | CommandDispatchResult>;
  cancelCommand?: (transactionId: string) => Promise<CommandTransaction | null>;
  retryCommand?: (transactionId: string) => Promise<CommandDispatchResult | null>;
  auditCommandRejection?: (request: GuardedCommandRequest, reason: string) => Promise<CommandAuditEntry>;
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
    __animusSetWorkspace?: (name: string) => void;
  }
}

const root = document.querySelector<HTMLDivElement>('#app');
if (!root) throw new Error('missing #app');

renderAppShell(root);

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
  syncInspection: true,
  settings: {
    schemaVersion: 1,
    defaultWorkspace: 'flight',
    theme: 'grid',
    cameraMode: 'chase',
    cameraLock: false,
    lastDashboardPresetLabel: null
  } as AnimusUiSettings,
  settingsLoaded: false
};
bindMapControls(() => state.snapshot);
const dashboard = createDashboardController({
  getSnapshot: () => state.snapshot,
  getApi: () => window.altairAnimus,
  issueCommand: (command, originSurface) => issueGuardedCommand(command, originSurface),
  setStatus: (message) => {
    document.querySelector<HTMLElement>('#status')!.textContent = message;
  },
  onPresetApplied: (label) => {
    saveUiSettings({ lastDashboardPresetLabel: label });
  }
});

function applyVehicle(message: VehicleStateMessage): void {
  state.selected = message;
  updateHud(message, state.showYaw);
  updateStatusStrip(message);
  scene.applyVehicle(message);
  const snapshot = mapSnapshotForVehicle(message);
  drawMap(snapshot);
  dashboard.update(snapshot);
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
  dashboard.update(snapshot);
}

function updateConfig(config: AnimusConfig): void {
  document.querySelector<HTMLInputElement>('#listen-port')!.value = String(config.listenPort);
  document.querySelector<HTMLInputElement>('#qgc')!.checked = config.qgcForwarding;
  const endpoints = config.qgcEndpoints.map((endpoint) => `${endpoint.host}:${endpoint.port}`).join(', ');
  document.querySelector<HTMLElement>('#config')!.textContent = `UDP ${config.listenHost}:${config.listenPort} / QGC ${config.qgcForwarding ? endpoints : 'off'} / ${config.authorityMode}${config.qgcForwarding && config.writableAnimus ? ' / duplicate-GCS risk' : ''}`;
}

function setWorkspace(name: string, persist = true): void {
  document.querySelectorAll<HTMLButtonElement>('[data-workspace]').forEach((button) => button.classList.toggle('active', button.dataset.workspace === name));
  document.querySelectorAll<HTMLElement>('.workspace-panel').forEach((panel) => panel.classList.toggle('workspace-visible', panel.dataset.panel === name || panel.dataset.panel === 'session'));
  if (state.snapshot) drawMap(state.snapshot);
  else if (state.selected) drawMap(mapSnapshotForVehicle(state.selected));
  if (persist && isWorkspaceName(name)) saveUiSettings({ defaultWorkspace: name });
}

window.__animusSetWorkspace = setWorkspace;

function mapSnapshotForVehicle(message: VehicleStateMessage): SessionSnapshotMessage {
  if (!state.snapshot) {
    return {
      type: 'session_snapshot',
      vehicles: [message],
      selectedVehicleId: message.id ?? null,
      messages: [],
      events: [],
      packetCount: 0,
      decodedCount: 0
    };
  }
  const selectedId = state.snapshot.selectedVehicleId ?? message.id ?? null;
  const messageKey = message.id ?? `${message.systemId ?? '--'}:${message.componentId ?? '--'}`;
  const vehicles = state.snapshot.vehicles.some((vehicle) => (vehicle.id ?? `${vehicle.systemId ?? '--'}:${vehicle.componentId ?? '--'}`) === messageKey)
    ? state.snapshot.vehicles.map((vehicle) => ((vehicle.id ?? `${vehicle.systemId ?? '--'}:${vehicle.componentId ?? '--'}`) === messageKey ? message : vehicle))
    : [...state.snapshot.vehicles, message];
  return { ...state.snapshot, vehicles, selectedVehicleId: selectedId };
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
    return `<div><strong>${escapeHtml(id)}</strong><span>${escapeHtml(vehicle.status?.firmware?.label ?? vehicle.vehicleType ?? 'MAVLink')}</span><span>${escapeHtml(vehicle.status?.modeState?.label ?? vehicle.status?.mode ?? '--')}</span><span>${armed}</span><span>${position}</span></div>`;
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
  updateSetupSurface(selected, snapshot);
  const mock = snapshot.mockLinks?.[0];
  if (mock) document.querySelector<HTMLElement>('#mock-status')!.textContent = `${mock.label} / ${mock.diagnostics.status}`;
}

function updateCameraStreams(vehicle: VehicleStateMessage | null): void {
  const streams = vehicle?.cameraStreams ?? [];
  document.querySelector<HTMLElement>('#camera-streams')!.innerHTML = streams.map((stream) => `
    <div><strong>${escapeHtml(stream.label)}</strong><span>${escapeHtml(stream.kind.toUpperCase())}</span><span>${escapeHtml(stream.status)}</span><span>${escapeHtml(stream.uri ?? stream.resolution ?? 'metadata')}</span></div>
  `).join('') || '<p class="empty">No RTP, RTSP, UVC, or MAVLink camera metadata streams advertised</p>';
  document.querySelector<HTMLElement>('#camera-detail')!.innerHTML = operationRows('camera') || '<p class="empty">No camera operations</p>';
  const writable = Boolean(vehicle?.commandCapabilities?.writableLink);
  document.querySelectorAll<HTMLButtonElement>('[data-camera-action]').forEach((button) => {
    button.disabled = !writable || streams.length === 0;
    button.title = writable ? 'Send MAVLink camera operation' : vehicle?.commandCapabilities?.blockedReason ?? 'read-only link';
  });
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
    .map((issue) => `<div><strong>${escapeHtml(issue.severity)}</strong><span>${escapeHtml(`${issue.path}: ${issue.message}`)}</span></div>`)
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
  const download = document.querySelector<HTMLButtonElement>('#plan-download');
  if (download) download.disabled = !vehicle?.commandCapabilities?.liveLink;
  const clear = document.querySelector<HTMLButtonElement>('#plan-clear');
  if (clear) clear.disabled = !vehicle?.commandCapabilities?.writableLink;
  document.querySelector<HTMLElement>('#mission-operations')!.innerHTML = operationRows('mission') || '<p class="empty">No mission transfers</p>';
  const fences = vehicle?.geofences ?? [];
  const rally = vehicle?.rallyPoints ?? [];
  const terrain = vehicle?.terrain;
  document.querySelector<HTMLElement>('#fence-list')!.innerHTML = [
    vehicle?.home ? `<div><strong>Home</strong><span>${vehicle.home.latDeg.toFixed(6)}</span><span>${vehicle.home.lonDeg.toFixed(6)}</span><span>${vehicle.home.altitudeM.toFixed(1)} m</span></div>` : '',
    ...fences.map((zone) => `<div><strong>${escapeHtml(zone.id)}</strong><span>${zone.kind}</span><span>${zone.inclusion ? 'include' : 'exclude'}</span></div>`),
    ...rally.map((point) => `<div><strong>${escapeHtml(point.id)}</strong><span>rally</span><span>${point.altitudeM?.toFixed(1) ?? '--'} m</span></div>`),
    terrain ? `<div><strong>Terrain</strong><span>${terrain.spacingM ?? '--'} m</span><span>${terrain.terrainHeightM?.toFixed(1) ?? '--'} / ${terrain.currentHeightM?.toFixed(1) ?? '--'} m</span><span>${terrain.loaded ?? '--'} loaded</span></div>` : '',
    operationRows('terrain')
  ].join('') || '<p class="empty">No geofence, rally, or terrain records</p>';
}

function updateSetupSurface(vehicle: VehicleStateMessage | null, snapshot = state.snapshot): void {
  const readiness = vehicle?.status?.readiness;
  const checks = readiness?.checks ?? [
    { label: 'Live link', state: vehicle?.connected ? 'ready' : 'blocked', detail: vehicle?.connected ? 'fresh MAVLink packets' : 'missing' },
    { label: 'Firmware', state: 'unknown', detail: 'firmware identity has not been decoded' }
  ];
  const commandState = vehicle?.commandCapabilities;
  document.querySelector<HTMLElement>('#readiness-list')!.innerHTML = [
    `<div><strong>Overall</strong><span>${escapeHtml(readiness?.overall ?? 'unknown')}</span><span>${escapeHtml(commandState?.blockedReason ?? 'commands available')}</span></div>`,
    `<div><strong>Authority</strong><span>${escapeHtml(commandState?.authority ?? 'unknown')}</span><span>${escapeHtml(commandState?.writableLink ? 'writable' : 'read-only')}</span></div>`,
    `<div><strong>Mode</strong><span>${escapeHtml(vehicle?.status?.modeState?.label ?? '--')}</span><span>${escapeHtml(vehicle?.status?.modeState?.unsupportedReason ?? vehicle?.status?.modeState?.category ?? 'unknown')}</span></div>`,
    `<div><strong>Failsafe</strong><span>${escapeHtml(vehicle?.status?.failsafeState?.status ?? 'unknown')}</span><span>${escapeHtml(vehicle?.status?.failsafeState?.label ?? 'MAVLink system status has not been decoded')}</span></div>`,
    `<div><strong>Mission</strong><span>${escapeHtml(vehicle?.status?.missionState?.state ?? 'unknown')}</span><span>${escapeHtml(vehicle?.status?.missionState?.detail ?? 'mission state has not been decoded')}</span></div>`,
    ...checks.map((check) => `<div><strong>${escapeHtml(String(check.label))}</strong><span>${escapeHtml(String(check.state))}</span><span>${escapeHtml(String(check.detail))}</span></div>`)
  ].join('');
  const parameters = vehicle?.parameters ?? [];
  const query = document.querySelector<HTMLInputElement>('#parameter-filter')?.value.toLowerCase() ?? '';
  document.querySelector<HTMLElement>('#parameter-list')!.innerHTML = parameters
    .filter((param) => param.name.toLowerCase().includes(query))
    .slice(0, 80)
    .map((param) => `<div><strong>${escapeHtml(param.name)}</strong><span>${escapeHtml(String(param.value))}</span><span>${escapeHtml(param.type)}</span><button type="button" data-param-set="${escapeHtml(param.name)}" ${vehicle?.commandCapabilities?.writableLink && !param.readonly ? '' : 'disabled'}>Set</button></div>`)
    .join('') || '<p class="empty">No parameters loaded</p>';
  document.querySelectorAll<HTMLButtonElement>('[data-param-set]').forEach((button) => {
    button.addEventListener('click', () => {
      const name = button.dataset.paramSet ?? '';
      const param = parameters.find((candidate) => candidate.name === name);
      const raw = window.prompt(`Set ${name}`, String(param?.value ?? ''));
      if (raw === null || !param || !state.selected) return;
      const vehicleId = state.selected.id ?? `${state.selected.systemId ?? '--'}:${state.selected.componentId ?? '--'}`;
      void window.altairAnimus?.setParameter?.({ vehicleId, name, value: Number(raw), confirmed: true, originSurface: 'setup-parameters' }).then((result) => {
        document.querySelector<HTMLElement>('#status')!.textContent = result.reason;
      });
    });
  });
  document.querySelector<HTMLElement>('#onboard-log-listing')!.innerHTML = [
    ...(vehicle?.logs ?? []).map((log) => `<div><strong>Log ${log.id}</strong><span>${log.sizeBytes} bytes</span><span>${log.timeUtc ?? '--'}</span><button type="button" data-log-download="${log.id}">Download</button></div>`),
    operationRows('logs'),
    operationRows('parameters')
  ].join('') || '<p class="empty">No onboard log list loaded</p>';
  document.querySelectorAll<HTMLButtonElement>('[data-log-download]').forEach((button) => {
    button.addEventListener('click', () => {
      const vehicleId = state.selected?.id ?? `${state.selected?.systemId ?? '--'}:${state.selected?.componentId ?? '--'}`;
      void window.altairAnimus?.downloadOnboardLog?.(Number(button.dataset.logDownload), vehicleId).then((result) => {
        document.querySelector<HTMLElement>('#status')!.textContent = result.reason;
      });
    });
  });
  const diag = vehicle?.diagnostics;
  document.querySelector<HTMLElement>('#diagnostics-list')!.innerHTML = diag
    ? `<div><strong>${escapeHtml(diag.linkId)}</strong><span>${escapeHtml(diag.transport)}</span><span>${escapeHtml(diag.status)}</span><span>${diag.packetsRx} rx</span></div>
       <div><strong>Protocol</strong><span>${escapeHtml(diag.protocol?.mavlinkVersion ?? 'unknown')}</span><span>${escapeHtml(diag.protocol?.signed ? 'signed' : diag.protocol?.signingRequired ? 'signing required' : 'unsigned')}</span><span>${escapeHtml(diag.protocol?.dialectCoverage ?? 'unknown')}</span></div>
       <div><strong>Forwarding</strong><span>${escapeHtml(diag.qgcForwarding ? 'QGC on' : 'QGC off')}</span><span>${escapeHtml(diag.selectedWritableEndpoint ?? 'no writable endpoint')}</span><span>${escapeHtml(diag.duplicateGcsForwardingRisk ? 'duplicate GCS risk' : 'single writer')}</span></div>`
    : '<p class="empty">No link diagnostics</p>';
  renderGuardedCommands(vehicle);
  renderCommandHistory(snapshot);
}

function renderCommandHistory(snapshot: SessionSnapshotMessage | null): void {
  const container = document.querySelector<HTMLElement>('#command-history')!;
  const audit = snapshot?.commandAudit ?? [];
  const transactions = snapshot?.commandTransactions ?? [];
  const cancellable = transactions.filter((transaction) => transaction.cancellationEligible);
  if (audit.length > 0 && cancellable.length === 0) {
    container.innerHTML = audit.slice(0, 8)
      .map((entry) => {
        const detail = entry.ack?.label ?? entry.reason;
        const context = `${entry.operatorId ?? '--'} / ${entry.sessionId ?? '--'} / retry ${entry.retryCount ?? 0}`;
        return `<div><strong>${escapeHtml(entry.commandName)}</strong><span>${escapeHtml(entry.state)}</span><span>${escapeHtml(detail)}</span><span>${escapeHtml(context)}</span><span>${escapeHtml(formatAuditTime(entry.timestamp))}</span></div>`;
      })
      .join('');
    return;
  }
  container.innerHTML = transactions.slice(0, 8)
    .map((transaction) => {
      const detail = transaction.ack?.label ?? transaction.failureReason ?? 'awaiting COMMAND_ACK';
      const action = transaction.cancellationEligible
        ? `<button type="button" data-cancel-command="${escapeHtml(transaction.id)}">Cancel</button>`
        : transaction.retryEligible
          ? `<button type="button" data-retry-command="${escapeHtml(transaction.id)}">Retry</button>`
          : `<span>${formatTime(transaction.updatedAtS)}</span>`;
      return `<div><strong>${escapeHtml(transaction.commandName)}</strong><span>${escapeHtml(transaction.state)}</span><span>${escapeHtml(detail)}</span><span>retry ${transaction.retryCount}</span>${action}</div>`;
    })
    .join('') || '<p class="empty">No command transactions</p>';
  container.querySelectorAll<HTMLButtonElement>('[data-cancel-command]').forEach((button) => {
    button.addEventListener('click', () => {
      void window.altairAnimus?.cancelCommand?.(button.dataset.cancelCommand ?? '').then(() => {
        document.querySelector<HTMLElement>('#status')!.textContent = 'Command cancellation recorded';
      });
    });
  });
  container.querySelectorAll<HTMLButtonElement>('[data-retry-command]').forEach((button) => {
    button.addEventListener('click', () => {
      const transaction = transactions.find((candidate) => candidate.id === button.dataset.retryCommand);
      if (!transaction || !confirmRetryCommand(transaction)) return;
      void window.altairAnimus?.retryCommand?.(transaction.id).then((result) => {
        if (result) document.querySelector<HTMLElement>('#status')!.textContent = result.reason;
      });
    });
  });
}

function operationRows(domain: string): string {
  const operations = state.snapshot?.protocolOperations?.filter((operation) => operation.domain === domain).slice(0, 8) ?? [];
  return operations.map((operation) => {
    const progress = operation.progressPct === null ? '--' : `${operation.progressPct.toFixed(0)}%`;
    const detail = operation.failureReason ?? operation.resultSummary ?? `${operation.sentPackets} tx / ${operation.receivedPackets} rx`;
    return `<div><strong>${escapeHtml(operation.action)}</strong><span>${escapeHtml(operation.state)}</span><span>${escapeHtml(progress)}</span><span>${escapeHtml(detail)}</span></div>`;
  }).join('');
}

function renderGuardedCommands(vehicle: VehicleStateMessage | null): void {
  const normalCommands: CommandName[] = ['arm', 'takeoff', 'change-altitude', 'go-to', 'orbit', 'mission-start', 'mission-continue', 'mission-resume'];
  const safetyCommands: CommandName[] = ['disarm', 'emergency-stop', 'pause', 'return-to-launch', 'land'];
  const container = document.querySelector<HTMLElement>('#guarded-commands')!;
  const supported = vehicle?.commandCapabilities?.supported ?? [];
  const blockedReason = vehicle?.commandCapabilities?.blockedReason ?? 'command is not advertised by the selected vehicle';
  const blockedCommands = vehicle?.commandCapabilities?.blockedCommands ?? {};
  const renderButton = (command: CommandName, group: 'normal' | 'safety'): string => {
    const enabled = supported.includes(command);
    const title = enabled ? `Send ${command}` : blockedCommands[command] ?? blockedReason;
    return `<button type="button" class="command-${group}${command === 'emergency-stop' ? ' command-emergency' : ''}" data-command="${command}" ${enabled ? '' : 'disabled'} title="${escapeHtml(title)}">${escapeHtml(command)}</button>`;
  };
  container.innerHTML = `
    <div class="command-group" aria-label="Normal actions">${normalCommands.map((command) => renderButton(command, 'normal')).join('')}</div>
    <div class="command-group command-group-safety" aria-label="Safety actions">${safetyCommands.map((command) => renderButton(command, 'safety')).join('')}</div>
  `;
  container.querySelectorAll<HTMLButtonElement>('[data-command]').forEach((button) => {
    button.addEventListener('click', () => {
      issueGuardedCommand(button.dataset.command as CommandName, 'setup-guarded-commands');
    });
  });
}

function issueGuardedCommand(command: CommandName, originSurface: string): void {
  const vehicle = state.selected;
  const vehicleId = vehicle?.id ?? `${vehicle?.systemId ?? '--'}:${vehicle?.componentId ?? '--'}`;
  const confirmation = confirmGuardedCommand(command, vehicleId);
  if (!confirmation.confirmed) {
    void window.altairAnimus?.auditCommandRejection?.({ command, vehicleId, confirmed: false, confirmationType: confirmation.confirmationType, confirmationResult: 'rejected', originSurface, params: confirmation.params }, 'operator confirmation was rejected');
    return;
  }
  void window.altairAnimus?.issueCommand?.({ command, vehicleId, confirmed: true, confirmationType: confirmation.confirmationType, confirmationResult: 'accepted', originSurface, params: confirmation.params }).then((result) => {
    document.querySelector<HTMLElement>('#status')!.textContent = result.reason;
  });
}

function confirmGuardedCommand(command: CommandName, vehicleId: string): { confirmed: boolean; confirmationType: 'browser-confirm' | 'typed-vehicle-id' | 'typed-altitude'; params?: Record<string, number | string | boolean | null> } {
  if (requiresTypedVehicleId(command)) {
    return { confirmed: window.prompt(`Type ${vehicleId} to send ${command} to ${vehicleId}`) === vehicleId, confirmationType: 'typed-vehicle-id' };
  }
  if (requiresTypedAltitude(command)) {
    const fallback = command === 'takeoff' ? 50 : state.selected?.globalPosition.relativeAltitudeM ?? state.selected?.globalPosition.altitudeM ?? 50;
    const rawAltitude = window.prompt(`Type target altitude in meters for ${command}`, String(Math.round(fallback)));
    if (rawAltitude === null) return { confirmed: false, confirmationType: 'typed-altitude' };
    const altitudeM = Number(rawAltitude.trim());
    if (!Number.isFinite(altitudeM) || rawAltitude.trim() !== String(altitudeM)) {
      return { confirmed: false, confirmationType: 'typed-altitude' };
    }
    return { confirmed: true, confirmationType: 'typed-altitude', params: { altitudeM } };
  }
  return { confirmed: window.confirm(`Send ${command} to ${vehicleId}?`), confirmationType: 'browser-confirm' };
}

function confirmRetryCommand(transaction: CommandTransaction): boolean {
  return window.confirm(`Retry ${transaction.commandName} for ${transaction.vehicleId}? Current guards will be re-evaluated.`);
}

function requiresTypedVehicleId(command: CommandName): boolean {
  return command === 'emergency-stop' || command === 'disarm' || command === 'land' || command === 'return-to-launch';
}

function requiresTypedAltitude(command: CommandName): boolean {
  return command === 'takeoff' || command === 'change-altitude';
}

function formatTime(seconds: number): string {
  const clamped = Math.max(0, seconds);
  const minutes = Math.floor(clamped / 60);
  const wholeSeconds = Math.floor(clamped % 60);
  return `${minutes}:${wholeSeconds.toString().padStart(2, '0')}`;
}

function formatAuditTime(timestamp: string): string {
  const date = new Date(timestamp);
  if (Number.isNaN(date.getTime())) {
    return timestamp;
  }
  return date.toLocaleTimeString([], { hour: '2-digit', minute: '2-digit', second: '2-digit' });
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
  setTheme(next[scene.theme]);
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
  void window.altairAnimus?.uploadMission?.(state.mission, vehicleId, true).then((result) => {
    document.querySelector<HTMLElement>('#status')!.textContent = result.reason;
    updatePlanSurface(state.selected);
  });
});
document.querySelector<HTMLButtonElement>('#plan-download')!.addEventListener('click', () => {
  const vehicleId = state.selected?.id ?? `${state.selected?.systemId ?? '--'}:${state.selected?.componentId ?? '--'}`;
  void window.altairAnimus?.downloadMission?.(vehicleId).then((result) => {
    document.querySelector<HTMLElement>('#status')!.textContent = result.reason;
    if (result.plan) state.mission = result.plan;
    updatePlanSurface(state.selected);
  });
});
document.querySelector<HTMLButtonElement>('#plan-clear')!.addEventListener('click', () => {
  const vehicleId = state.selected?.id ?? `${state.selected?.systemId ?? '--'}:${state.selected?.componentId ?? '--'}`;
  if (window.prompt(`Type CLEAR MISSION to clear mission on ${vehicleId}`) !== 'CLEAR MISSION') return;
  void window.altairAnimus?.clearMission?.(vehicleId, true).then((result) => {
    document.querySelector<HTMLElement>('#status')!.textContent = result.reason;
  });
});
document.querySelector<HTMLInputElement>('#parameter-filter')!.addEventListener('input', () => {
  if (state.snapshot) updateSetupSurface(state.selected);
});
document.querySelector<HTMLButtonElement>('#parameter-refresh')!.addEventListener('click', () => {
  const vehicleId = state.selected?.id ?? `${state.selected?.systemId ?? '--'}:${state.selected?.componentId ?? '--'}`;
  void window.altairAnimus?.refreshParameters?.(vehicleId).then((result) => {
    document.querySelector<HTMLElement>('#status')!.textContent = result.reason;
  });
});
document.querySelector<HTMLButtonElement>('#onboard-log-list')!.addEventListener('click', () => {
  const vehicleId = state.selected?.id ?? `${state.selected?.systemId ?? '--'}:${state.selected?.componentId ?? '--'}`;
  void window.altairAnimus?.listOnboardLogs?.(vehicleId).then((result) => {
    document.querySelector<HTMLElement>('#status')!.textContent = result.reason;
  });
});
document.querySelector<HTMLButtonElement>('#onboard-log-erase')!.addEventListener('click', () => {
  const vehicleId = state.selected?.id ?? `${state.selected?.systemId ?? '--'}:${state.selected?.componentId ?? '--'}`;
  if (window.prompt(`Type ERASE LOGS to erase onboard logs on ${vehicleId}`) !== 'ERASE LOGS') return;
  void window.altairAnimus?.eraseOnboardLogs?.(vehicleId, true).then((result) => {
    document.querySelector<HTMLElement>('#status')!.textContent = result.reason;
  });
});
document.querySelector<HTMLButtonElement>('#terrain-request')!.addEventListener('click', () => {
  const vehicleId = state.selected?.id ?? `${state.selected?.systemId ?? '--'}:${state.selected?.componentId ?? '--'}`;
  void window.altairAnimus?.requestTerrain?.(vehicleId).then((result) => {
    document.querySelector<HTMLElement>('#status')!.textContent = result.reason;
  });
});
document.querySelectorAll<HTMLButtonElement>('[data-camera-action]').forEach((button) => button.addEventListener('click', () => {
  const vehicleId = state.selected?.id ?? `${state.selected?.systemId ?? '--'}:${state.selected?.componentId ?? '--'}`;
  const action = button.dataset.cameraAction;
  const promise = action === 'capture'
    ? window.altairAnimus?.cameraCapture?.(vehicleId)
    : action === 'record-start'
      ? window.altairAnimus?.cameraRecord?.(true, vehicleId)
      : action === 'record-stop'
        ? window.altairAnimus?.cameraRecord?.(false, vehicleId)
        : window.altairAnimus?.cameraSetSetting?.(action === 'focus' ? 'focus' : 'zoom', Number(window.prompt(`Set ${action}`, '50') ?? '50'), vehicleId);
  void promise?.then((result) => {
    document.querySelector<HTMLElement>('#status')!.textContent = result.reason;
  });
}));
document.querySelectorAll<HTMLButtonElement>('[data-camera]').forEach((button) => button.addEventListener('click', () => setCameraMode(button.dataset.camera as CameraMode)));
document.querySelector<HTMLButtonElement>('#camera-lock')!.addEventListener('click', () => setCameraLocked(!scene.cameraLocked));
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
    'authority-state': `Command authority: ${selected?.commandCapabilities?.authority ?? 'unknown'} / ${selected?.commandCapabilities?.blockedReason ?? 'available'}`,
    'arm-state': `Arming state: ${selected?.status?.armed === null || selected?.status?.armed === undefined ? '--' : selected.status.armed ? 'armed' : 'disarmed'}`,
    'mode-state': `Mode: ${selected?.status?.modeState?.label ?? '--'} / ${selected?.status?.modeState?.unsupportedReason ?? selected?.status?.modeState?.category ?? '--'}`,
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
  if (event.code === 'KeyC') setCameraMode(nextCameraMode(scene.cameraMode));
  if (event.code === 'KeyH') document.querySelector<HTMLButtonElement>(`[data-hud="${state.hudMode === 'console' ? 'tactical' : state.hudMode === 'tactical' ? 'off' : 'console'}"]`)?.click();
  if (event.code === 'KeyO') document.querySelector<HTMLButtonElement>('#ortho-toggle')!.click();
  if (event.code === 'KeyY') document.querySelector<HTMLButtonElement>('#heading-mode')!.click();
  if (event.code === 'KeyV') document.querySelector<HTMLButtonElement>('#theme-toggle')!.click();
  if (event.code === 'KeyM') document.querySelector<HTMLButtonElement>('#marker')!.click();
});

function setTheme(theme: ThemeName, persist = true): void {
  scene.setTheme(theme);
  if (persist) saveUiSettings({ theme });
}

function setCameraMode(mode: CameraMode, persist = true): void {
  scene.setCameraMode(mode);
  if (persist) saveUiSettings({ cameraMode: mode });
}

function setCameraLocked(locked: boolean, persist = true): void {
  scene.setCameraLocked(locked);
  if (persist) saveUiSettings({ cameraLock: locked });
}

function saveUiSettings(patch: Partial<AnimusUiSettings>): void {
  state.settings = { ...state.settings, ...patch, schemaVersion: 1 };
  if (!state.settingsLoaded) return;
  void window.altairAnimus?.saveSettings?.(state.settings).then((saved) => {
    state.settings = saved;
  }).catch(() => {
    document.querySelector<HTMLElement>('#status')!.textContent = 'Animus settings save failed';
  });
}

function isWorkspaceName(value: string): value is AnimusWorkspaceName {
  return ['flight', 'dashboard', 'map', 'inspector', 'video', 'plan', 'setup'].includes(value);
}

function applyUiSettings(settings: AnimusUiSettings): void {
  state.settings = settings;
  setTheme(settings.theme, false);
  setCameraMode(settings.cameraMode, false);
  setCameraLocked(settings.cameraLock, false);
  setWorkspace(settings.defaultWorkspace, false);
  state.settingsLoaded = true;
}

setHudMode('console');
setTheme('grid', false);
setCameraLocked(false, false);
document.querySelector<HTMLCanvasElement>('#ortho')!.classList.toggle('hidden', !scene.ortho);
setWorkspace('flight', false);
dashboard.load();
void window.altairAnimus?.getSettings?.().then(applyUiSettings).catch(() => {
  state.settingsLoaded = true;
});
connect();
scene.start();
