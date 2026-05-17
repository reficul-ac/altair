import { bindMapControls, currentMapCacheBbox, drawMap, refreshMapCacheStatus, refreshMapLayout, setMapFollowSelected } from './map-panel';
import { clearInspectorLog, recordInspectorSnapshot, updateInspector } from './inspector-ui';
import { setHudMode, updateHud, updateStatusStrip, updateVehicleList, type HudMode } from './hud-ui';
import { SceneRenderer, nextCameraMode, type CameraMode, type ThemeName } from './scene-renderer';
import { createDashboardController } from './dashboard-ui';
import type { AnimusDashboardLayout } from './dashboard-types';
import {
  ANIMUS_DEM_CACHE_DEFAULT_ENCODING,
  ANIMUS_DEM_CACHE_DEFAULT_MAX_ZOOM,
  ANIMUS_DEM_CACHE_DEFAULT_TEMPLATE,
  ANIMUS_DEM_CACHE_DEFAULT_ATTRIBUTION,
  ANIMUS_MAP_CACHE_DEFAULT_ATTRIBUTION,
  ANIMUS_MAP_CACHE_DEFAULT_MAX_TILE_COUNT,
  ANIMUS_MAP_CACHE_DEFAULT_MAX_ZOOM,
  ANIMUS_MAP_CACHE_DEFAULT_MIN_ZOOM,
  ANIMUS_MAP_CACHE_DEFAULT_TEMPLATE,
  createUnavailableMapCacheStatus,
  normalizeMapCacheStatus,
  type MapCacheActionResult,
  type MapCacheEstimate,
  type MapCacheSet,
  type MapCacheStatus
} from './map-cache';
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
  TrailBuffer,
  trailPointFromState,
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
  schemaVersion: 3;
  defaultWorkspace: AnimusWorkspaceName;
  theme: ThemeName;
  cameraMode: CameraMode;
  cameraLock: boolean;
  mapStyle: 'satellite';
  mapFollowSelected: boolean;
  mapTileUrlTemplate: string;
  mapTileAttribution: string;
  activeMapCacheSetId: string | null;
  mapCacheMinZoom: number;
  mapCacheMaxZoom: number;
  mapCacheMaxTileCount: number;
  demTileUrlTemplate: string;
  demTileAttribution: string;
  demTileEncoding: 'terrarium' | 'mapbox';
  activeDemCacheSetId: string | null;
  demCacheMinZoom: number;
  demCacheMaxZoom: number;
  demCacheMaxTileCount: number;
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
  getMapCacheStatus?: () => Promise<MapCacheStatus>;
  estimateMapCache?: (request: { bbox: ReturnType<typeof currentMapCacheBbox>; minZoom: number; maxZoom: number; maxTileCount?: number }) => Promise<MapCacheEstimate>;
  startMapCacheDownload?: (request: { urlTemplate: string; attribution?: string | null; label?: string | null; bbox: ReturnType<typeof currentMapCacheBbox>; minZoom: number; maxZoom: number; maxTileCount?: number }) => Promise<MapCacheActionResult>;
  cancelMapCacheDownload?: () => Promise<MapCacheActionResult>;
  listMapCacheSets?: () => Promise<MapCacheSet[]>;
  activateMapCacheSet?: (setId: string) => Promise<MapCacheActionResult>;
  deleteMapCacheSet?: (setId: string) => Promise<MapCacheActionResult>;
  getDemCacheStatus?: () => Promise<MapCacheStatus>;
  estimateDemCache?: (request: { bbox: ReturnType<typeof currentMapCacheBbox>; minZoom: number; maxZoom: number; maxTileCount?: number }) => Promise<MapCacheEstimate>;
  startDemCacheDownload?: (request: { urlTemplate: string; attribution?: string | null; label?: string | null; encoding?: 'terrarium' | 'mapbox'; bbox: ReturnType<typeof currentMapCacheBbox>; minZoom: number; maxZoom: number; maxTileCount?: number }) => Promise<MapCacheActionResult>;
  cancelDemCacheDownload?: () => Promise<MapCacheActionResult>;
  listDemCacheSets?: () => Promise<MapCacheSet[]>;
  activateDemCacheSet?: (setId: string) => Promise<MapCacheActionResult>;
  deleteDemCacheSet?: (setId: string) => Promise<MapCacheActionResult>;
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
  onLayoutRefresh?: (callback: (message: { reason: string; width: number; height: number }) => void) => () => void;
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
    __animusRefreshLayout?: (reason?: string) => void;
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
    schemaVersion: 3,
    defaultWorkspace: 'flight',
    theme: 'grid',
    cameraMode: 'chase',
    cameraLock: false,
    mapStyle: 'satellite',
    mapFollowSelected: true,
    mapTileUrlTemplate: ANIMUS_MAP_CACHE_DEFAULT_TEMPLATE,
    mapTileAttribution: ANIMUS_MAP_CACHE_DEFAULT_ATTRIBUTION,
    activeMapCacheSetId: null,
    mapCacheMinZoom: ANIMUS_MAP_CACHE_DEFAULT_MIN_ZOOM,
    mapCacheMaxZoom: ANIMUS_MAP_CACHE_DEFAULT_MAX_ZOOM,
    mapCacheMaxTileCount: ANIMUS_MAP_CACHE_DEFAULT_MAX_TILE_COUNT,
    demTileUrlTemplate: ANIMUS_DEM_CACHE_DEFAULT_TEMPLATE,
    demTileAttribution: ANIMUS_DEM_CACHE_DEFAULT_ATTRIBUTION,
    demTileEncoding: ANIMUS_DEM_CACHE_DEFAULT_ENCODING,
    activeDemCacheSetId: null,
    demCacheMinZoom: ANIMUS_MAP_CACHE_DEFAULT_MIN_ZOOM,
    demCacheMaxZoom: ANIMUS_DEM_CACHE_DEFAULT_MAX_ZOOM,
    demCacheMaxTileCount: ANIMUS_MAP_CACHE_DEFAULT_MAX_TILE_COUNT,
    lastDashboardPresetLabel: null
  } as AnimusUiSettings,
  settingsLoaded: false
};
let mapCachePoll: number | null = null;
const MAP_PROVIDER_DEFAULT = 'altair-usgs';
const DEM_PROVIDER_DEFAULT = 'altair-terrarium';
const mapTrails = new Map<string, TrailBuffer>();
bindMapControls(() => state.snapshot, (followSelected) => saveUiSettings({ mapFollowSelected: followSelected }));
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
  const mapSnapshot = snapshotWithMapTrails(snapshot);
  state.snapshot = mapSnapshot;
  recordInspectorSnapshot(mapSnapshot, state.replay?.loaded ? state.replay.timestampS : performance.now() / 1000);
  const selected = mapSnapshot.vehicles.find((vehicle) => vehicle.id === mapSnapshot.selectedVehicleId) ?? mapSnapshot.vehicles[0] ?? null;
  if (selected) applyVehicle(selected);
  scene.applyFleet(mapSnapshot.vehicles, mapSnapshot.selectedVehicleId, mapSnapshot.events);
  updateVehicleList(mapSnapshot, (id) => void window.altairAnimus?.selectVehicle?.(id).then(applySnapshot));
  updateInspector(mapSnapshot);
  drawMap(mapSnapshot);
  updateVehicleComparison(mapSnapshot);
  updateAnalysis(mapSnapshot);
  updateGcsSurfaces(mapSnapshot);
  dashboard.update(mapSnapshot);
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
  refreshLayout('workspace');
  if (state.snapshot) drawMap(state.snapshot);
  else if (state.selected) drawMap(mapSnapshotForVehicle(state.selected));
  if (persist && isWorkspaceName(name)) saveUiSettings({ defaultWorkspace: name });
}

window.__animusSetWorkspace = setWorkspace;
window.__animusRefreshLayout = refreshLayout;

function refreshLayout(_reason = 'manual'): void {
  scene.resizeNow();
  refreshMapLayout();
}

function scheduleLayoutRefresh(reason: string): void {
  refreshLayout(reason);
  window.requestAnimationFrame(() => refreshLayout(reason));
  window.setTimeout(() => refreshLayout(reason), 120);
}

window.addEventListener('resize', () => scheduleLayoutRefresh('window-resize'));
const layoutObserver = new ResizeObserver(() => scheduleLayoutRefresh('resize-observer'));
layoutObserver.observe(shell);
const viewport = document.querySelector<HTMLElement>('.viewport');
if (viewport) layoutObserver.observe(viewport);
window.altairAnimus?.onLayoutRefresh?.((message) => scheduleLayoutRefresh(message.reason));

function mapSnapshotForVehicle(message: VehicleStateMessage): SessionSnapshotMessage {
  const mapVehicle = vehicleWithMapTrail(message);
  if (!state.snapshot) {
    return {
      type: 'session_snapshot',
      vehicles: [mapVehicle],
      selectedVehicleId: mapVehicle.id ?? null,
      messages: [],
      events: [],
      packetCount: 0,
      decodedCount: 0
    };
  }
  const selectedId = state.snapshot.selectedVehicleId ?? mapVehicle.id ?? null;
  const messageKey = mapVehicle.id ?? `${mapVehicle.systemId ?? '--'}:${mapVehicle.componentId ?? '--'}`;
  const vehicles = state.snapshot.vehicles.some((vehicle) => (vehicle.id ?? `${vehicle.systemId ?? '--'}:${vehicle.componentId ?? '--'}`) === messageKey)
    ? state.snapshot.vehicles.map((vehicle) => ((vehicle.id ?? `${vehicle.systemId ?? '--'}:${vehicle.componentId ?? '--'}`) === messageKey ? mapVehicle : vehicle))
    : [...state.snapshot.vehicles, mapVehicle];
  return { ...state.snapshot, vehicles, selectedVehicleId: selectedId };
}

function snapshotWithMapTrails(snapshot: SessionSnapshotMessage): SessionSnapshotMessage {
  return { ...snapshot, vehicles: snapshot.vehicles.map(vehicleWithMapTrail) };
}

function vehicleWithMapTrail(vehicle: VehicleStateMessage): VehicleStateMessage {
  const key = vehicle.id ?? `${vehicle.systemId ?? '--'}:${vehicle.componentId ?? '--'}`;
  const trail = mapTrails.get(key) ?? new TrailBuffer();
  mapTrails.set(key, trail);
  const point = trailPointFromState(vehicle);
  if (point) trail.add(point);
  const points = trail.values();
  return { ...vehicle, trail: points.length > 0 ? [...points] : vehicle.trail };
}

function connect(): void {
  if (window.altairAnimus?.onVehicleState && window.altairAnimus.getConfig) {
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
document.querySelector<HTMLButtonElement>('#clear')!.addEventListener('click', () => {
  scene.clearTrail();
  mapTrails.clear();
  if (state.snapshot) drawMap(snapshotWithMapTrails(state.snapshot));
});
document.querySelector<HTMLButtonElement>('#heading-mode')!.addEventListener('click', (event) => {
  state.showYaw = !state.showYaw;
  (event.currentTarget as HTMLButtonElement).textContent = state.showYaw ? 'YAW' : 'TRK';
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
document.querySelector<HTMLButtonElement>('#map-cache-estimate')?.addEventListener('click', () => {
  void estimateMapCache();
});
document.querySelector<HTMLButtonElement>('#map-cache-download')?.addEventListener('click', () => {
  void startMapCacheDownload();
});
document.querySelector<HTMLButtonElement>('#map-cache-cancel')?.addEventListener('click', () => {
  void window.altairAnimus?.cancelMapCacheDownload?.().then((result) => applyMapCacheStatus(result.status));
});
document.querySelector<HTMLButtonElement>('#dem-cache-estimate')?.addEventListener('click', () => {
  void estimateDemCache();
});
document.querySelector<HTMLButtonElement>('#dem-cache-download')?.addEventListener('click', () => {
  void startDemCacheDownload();
});
document.querySelector<HTMLButtonElement>('#dem-cache-cancel')?.addEventListener('click', () => {
  void window.altairAnimus?.cancelDemCacheDownload?.().then((result) => applyDemCacheStatus(result.status));
});
document.querySelector<HTMLSelectElement>('#map-cache-provider')?.addEventListener('change', (event) => {
  if ((event.currentTarget as HTMLSelectElement).value === MAP_PROVIDER_DEFAULT) applyDefaultMapPreset();
});
document.querySelector<HTMLSelectElement>('#dem-cache-provider')?.addEventListener('change', (event) => {
  if ((event.currentTarget as HTMLSelectElement).value === DEM_PROVIDER_DEFAULT) applyDefaultDemPreset();
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
  state.settings = { ...state.settings, ...patch, schemaVersion: 3 };
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
  populateMapCacheInputs(settings);
  setTheme(settings.theme, false);
  setCameraMode(settings.cameraMode, false);
  setCameraLocked(settings.cameraLock, false);
  setMapFollowSelected(settings.mapFollowSelected);
  setWorkspace(settings.defaultWorkspace, false);
  state.settingsLoaded = true;
}

function applyMapCacheStatus(status: MapCacheStatus): void {
  const normalized = normalizeMapCacheStatus(status);
  scene.setMapCacheStatus(normalized);
  renderSetupMapCacheStatus(normalized);
  void refreshMapCacheStatus();
  scheduleMapCachePoll(normalized);
  if (state.snapshot) drawMap(state.snapshot);
}

function applyDemCacheStatus(status: MapCacheStatus): void {
  const normalized = normalizeMapCacheStatus(status);
  scene.setDemCacheStatus(normalized);
  renderSetupDemCacheStatus(normalized);
  void refreshMapCacheStatus();
  scheduleDemCachePoll(normalized);
  if (state.snapshot) drawMap(state.snapshot);
}

function scheduleMapCachePoll(status: MapCacheStatus): void {
  if (!status.downloadState?.active || mapCachePoll !== null) return;
  mapCachePoll = window.setTimeout(() => {
    mapCachePoll = null;
    void window.altairAnimus?.getMapCacheStatus?.().then(applyMapCacheStatus);
  }, 1000);
}

function scheduleDemCachePoll(status: MapCacheStatus): void {
  if (!status.downloadState?.active || mapCachePoll !== null) return;
  mapCachePoll = window.setTimeout(() => {
    mapCachePoll = null;
    void window.altairAnimus?.getDemCacheStatus?.().then(applyDemCacheStatus);
  }, 1000);
}

function renderSetupMapCacheStatus(status: MapCacheStatus): void {
  const target = document.querySelector<HTMLElement>('#setup-map-cache-status');
  if (!target) return;
  const row = (label: string, available: boolean, detail: string | null | undefined): string =>
    `<div><strong>${escapeHtml(label)}</strong><span>${available ? 'ready' : 'unavailable'}</span><span>${escapeHtml(detail ?? '--')}</span></div>`;
  const download = status.downloadState;
  const setRows = status.sets.map((set) => `
    <div><strong>${escapeHtml(set.label)}</strong><span>${set.id === status.activeSet?.id ? 'active' : `${set.downloadedCount}/${set.tileCount}`}</span><span>${escapeHtml(`${set.minZoom}-${set.maxZoom} / ${formatBytes(set.bytes)} / ${set.templateHost}`)}</span><label class="startup-default"><input type="checkbox" data-map-cache-startup="${escapeHtml(set.id)}" ${set.id === state.settings.activeMapCacheSetId ? 'checked' : ''} />Startup</label><button type="button" data-map-cache-activate="${escapeHtml(set.id)}">Use</button><button type="button" data-map-cache-delete="${escapeHtml(set.id)}">Delete</button></div>
  `);
  target.innerHTML = [
    row('Active cache', Boolean(status.activeSet), status.activeSet ? `${status.activeSet.label} / ${status.activeSet.downloadedCount} tiles` : status.error),
    download ? row('Download', download.active, `${download.downloaded}/${download.downloaded + download.queued + download.failed} ok, ${download.failed} failed, ${formatBytes(download.bytes)}${download.lastError ? ` / ${download.lastError}` : ''}`) : '',
    ...setRows
  ].join('');
  target.querySelectorAll<HTMLButtonElement>('[data-map-cache-activate]').forEach((button) => {
    button.addEventListener('click', () => {
      void window.altairAnimus?.activateMapCacheSet?.(button.dataset.mapCacheActivate ?? '').then((result) => applyMapCacheStatus(result.status));
    });
  });
  target.querySelectorAll<HTMLButtonElement>('[data-map-cache-delete]').forEach((button) => {
    button.addEventListener('click', () => {
      const setId = button.dataset.mapCacheDelete ?? '';
      if (state.settings.activeMapCacheSetId === setId) saveUiSettings({ activeMapCacheSetId: null });
      void window.altairAnimus?.deleteMapCacheSet?.(setId).then((result) => applyMapCacheStatus(result.status));
    });
  });
  target.querySelectorAll<HTMLInputElement>('[data-map-cache-startup]').forEach((checkbox) => {
    checkbox.addEventListener('change', () => {
      const setId = checkbox.dataset.mapCacheStartup ?? '';
      saveUiSettings({ activeMapCacheSetId: checkbox.checked ? setId : null });
      renderSetupMapCacheStatus(status);
    });
  });
}

function renderSetupDemCacheStatus(status: MapCacheStatus): void {
  const target = document.querySelector<HTMLElement>('#setup-dem-cache-status');
  if (!target) return;
  const row = (label: string, available: boolean, detail: string | null | undefined): string =>
    `<div><strong>${escapeHtml(label)}</strong><span>${available ? 'ready' : 'unavailable'}</span><span>${escapeHtml(detail ?? '--')}</span></div>`;
  const download = status.downloadState;
  const setRows = status.sets.map((set) => `
    <div><strong>${escapeHtml(set.label)}</strong><span>${set.id === status.activeSet?.id ? 'active' : `${set.downloadedCount}/${set.tileCount}`}</span><span>${escapeHtml(`${set.encoding ?? 'terrarium'} / ${set.minZoom}-${set.maxZoom} / ${formatBytes(set.bytes)} / ${set.templateHost}`)}</span><label class="startup-default"><input type="checkbox" data-dem-cache-startup="${escapeHtml(set.id)}" ${set.id === state.settings.activeDemCacheSetId ? 'checked' : ''} />Startup</label><button type="button" data-dem-cache-activate="${escapeHtml(set.id)}">Use</button><button type="button" data-dem-cache-delete="${escapeHtml(set.id)}">Delete</button></div>
  `);
  target.innerHTML = [
    row('Active DEM', Boolean(status.activeSet), status.activeSet ? `${status.activeSet.label} / ${status.activeSet.downloadedCount} tiles / ${status.activeSet.encoding ?? 'terrarium'}` : status.error),
    download ? row('Download', download.active, `${download.downloaded}/${download.downloaded + download.queued + download.failed} ok, ${download.failed} failed, ${formatBytes(download.bytes)}${download.lastError ? ` / ${download.lastError}` : ''}`) : '',
    ...setRows
  ].join('');
  target.querySelectorAll<HTMLButtonElement>('[data-dem-cache-activate]').forEach((button) => {
    button.addEventListener('click', () => {
      void window.altairAnimus?.activateDemCacheSet?.(button.dataset.demCacheActivate ?? '').then((result) => applyDemCacheStatus(result.status));
    });
  });
  target.querySelectorAll<HTMLButtonElement>('[data-dem-cache-delete]').forEach((button) => {
    button.addEventListener('click', () => {
      const setId = button.dataset.demCacheDelete ?? '';
      if (state.settings.activeDemCacheSetId === setId) saveUiSettings({ activeDemCacheSetId: null });
      void window.altairAnimus?.deleteDemCacheSet?.(setId).then((result) => applyDemCacheStatus(result.status));
    });
  });
  target.querySelectorAll<HTMLInputElement>('[data-dem-cache-startup]').forEach((checkbox) => {
    checkbox.addEventListener('change', () => {
      const setId = checkbox.dataset.demCacheStartup ?? '';
      saveUiSettings({ activeDemCacheSetId: checkbox.checked ? setId : null });
      renderSetupDemCacheStatus(status);
    });
  });
}

function populateMapCacheInputs(settings: AnimusUiSettings): void {
  const provider = document.querySelector<HTMLSelectElement>('#map-cache-provider');
  const template = document.querySelector<HTMLInputElement>('#map-cache-template');
  const attribution = document.querySelector<HTMLInputElement>('#map-cache-attribution');
  const minZoom = document.querySelector<HTMLInputElement>('#map-cache-min-zoom');
  const maxZoom = document.querySelector<HTMLInputElement>('#map-cache-max-zoom');
  const maxTiles = document.querySelector<HTMLInputElement>('#map-cache-max-tiles');
  const startup = document.querySelector<HTMLInputElement>('#map-cache-startup-default');
  const demProvider = document.querySelector<HTMLSelectElement>('#dem-cache-provider');
  const demTemplate = document.querySelector<HTMLInputElement>('#dem-cache-template');
  const demAttribution = document.querySelector<HTMLInputElement>('#dem-cache-attribution');
  const demEncoding = document.querySelector<HTMLSelectElement>('#dem-cache-encoding');
  const demMinZoom = document.querySelector<HTMLInputElement>('#dem-cache-min-zoom');
  const demMaxZoom = document.querySelector<HTMLInputElement>('#dem-cache-max-zoom');
  const demMaxTiles = document.querySelector<HTMLInputElement>('#dem-cache-max-tiles');
  const demStartup = document.querySelector<HTMLInputElement>('#dem-cache-startup-default');
  if (provider) provider.value = settings.mapTileUrlTemplate === ANIMUS_MAP_CACHE_DEFAULT_TEMPLATE ? MAP_PROVIDER_DEFAULT : 'custom';
  if (template) template.value = settings.mapTileUrlTemplate;
  if (attribution) attribution.value = settings.mapTileAttribution;
  if (minZoom) minZoom.value = String(settings.mapCacheMinZoom);
  if (maxZoom) maxZoom.value = String(settings.mapCacheMaxZoom);
  if (maxTiles) maxTiles.value = String(settings.mapCacheMaxTileCount);
  if (startup) startup.checked = false;
  if (demProvider) demProvider.value = settings.demTileUrlTemplate === ANIMUS_DEM_CACHE_DEFAULT_TEMPLATE && settings.demTileEncoding === ANIMUS_DEM_CACHE_DEFAULT_ENCODING ? DEM_PROVIDER_DEFAULT : 'custom';
  if (demTemplate) demTemplate.value = settings.demTileUrlTemplate;
  if (demAttribution) demAttribution.value = settings.demTileAttribution;
  if (demEncoding) demEncoding.value = settings.demTileEncoding;
  if (demMinZoom) demMinZoom.value = String(settings.demCacheMinZoom);
  if (demMaxZoom) demMaxZoom.value = String(settings.demCacheMaxZoom);
  if (demMaxTiles) demMaxTiles.value = String(settings.demCacheMaxTileCount);
  if (demStartup) demStartup.checked = false;
}

function applyDefaultMapPreset(): void {
  const template = document.querySelector<HTMLInputElement>('#map-cache-template');
  const attribution = document.querySelector<HTMLInputElement>('#map-cache-attribution');
  const minZoom = document.querySelector<HTMLInputElement>('#map-cache-min-zoom');
  const maxZoom = document.querySelector<HTMLInputElement>('#map-cache-max-zoom');
  if (template) template.value = ANIMUS_MAP_CACHE_DEFAULT_TEMPLATE;
  if (attribution) attribution.value = ANIMUS_MAP_CACHE_DEFAULT_ATTRIBUTION;
  if (minZoom) minZoom.value = String(ANIMUS_MAP_CACHE_DEFAULT_MIN_ZOOM);
  if (maxZoom) maxZoom.value = String(ANIMUS_MAP_CACHE_DEFAULT_MAX_ZOOM);
  saveUiSettings({
    mapTileUrlTemplate: ANIMUS_MAP_CACHE_DEFAULT_TEMPLATE,
    mapTileAttribution: ANIMUS_MAP_CACHE_DEFAULT_ATTRIBUTION,
    mapCacheMinZoom: ANIMUS_MAP_CACHE_DEFAULT_MIN_ZOOM,
    mapCacheMaxZoom: ANIMUS_MAP_CACHE_DEFAULT_MAX_ZOOM
  });
}

function applyDefaultDemPreset(): void {
  const template = document.querySelector<HTMLInputElement>('#dem-cache-template');
  const attribution = document.querySelector<HTMLInputElement>('#dem-cache-attribution');
  const encoding = document.querySelector<HTMLSelectElement>('#dem-cache-encoding');
  const minZoom = document.querySelector<HTMLInputElement>('#dem-cache-min-zoom');
  const maxZoom = document.querySelector<HTMLInputElement>('#dem-cache-max-zoom');
  if (template) template.value = ANIMUS_DEM_CACHE_DEFAULT_TEMPLATE;
  if (attribution) attribution.value = ANIMUS_DEM_CACHE_DEFAULT_ATTRIBUTION;
  if (encoding) encoding.value = ANIMUS_DEM_CACHE_DEFAULT_ENCODING;
  if (minZoom) minZoom.value = String(ANIMUS_MAP_CACHE_DEFAULT_MIN_ZOOM);
  if (maxZoom) maxZoom.value = String(ANIMUS_DEM_CACHE_DEFAULT_MAX_ZOOM);
  saveUiSettings({
    demTileUrlTemplate: ANIMUS_DEM_CACHE_DEFAULT_TEMPLATE,
    demTileAttribution: ANIMUS_DEM_CACHE_DEFAULT_ATTRIBUTION,
    demTileEncoding: ANIMUS_DEM_CACHE_DEFAULT_ENCODING,
    demCacheMinZoom: ANIMUS_MAP_CACHE_DEFAULT_MIN_ZOOM,
    demCacheMaxZoom: ANIMUS_DEM_CACHE_DEFAULT_MAX_ZOOM
  });
}

function mapCacheForm() {
  const template = document.querySelector<HTMLInputElement>('#map-cache-template')?.value.trim() ?? '';
  const attribution = document.querySelector<HTMLInputElement>('#map-cache-attribution')?.value.trim() ?? '';
  const defaultProvider = document.querySelector<HTMLSelectElement>('#map-cache-provider')?.value === MAP_PROVIDER_DEFAULT;
  const label = document.querySelector<HTMLInputElement>('#map-cache-label')?.value.trim() || (defaultProvider ? 'Altair default: USGS Imagery' : '');
  const minZoom = Number(document.querySelector<HTMLInputElement>('#map-cache-min-zoom')?.value ?? ANIMUS_MAP_CACHE_DEFAULT_MIN_ZOOM);
  const maxZoom = Number(document.querySelector<HTMLInputElement>('#map-cache-max-zoom')?.value ?? ANIMUS_MAP_CACHE_DEFAULT_MAX_ZOOM);
  const maxTileCount = Number(document.querySelector<HTMLInputElement>('#map-cache-max-tiles')?.value ?? ANIMUS_MAP_CACHE_DEFAULT_MAX_TILE_COUNT);
  saveUiSettings({ mapTileUrlTemplate: template, mapTileAttribution: attribution, mapCacheMinZoom: minZoom, mapCacheMaxZoom: maxZoom, mapCacheMaxTileCount: maxTileCount });
  return { urlTemplate: template, attribution, label, bbox: currentMapCacheBbox(state.snapshot), minZoom, maxZoom, maxTileCount };
}

async function estimateMapCache(): Promise<void> {
  const request = mapCacheForm();
  const result = await window.altairAnimus?.estimateMapCache?.(request);
  if (!result) return;
  const target = document.querySelector<HTMLElement>('#setup-map-cache-status');
  if (target) {
    target.innerHTML = `<div><strong>Estimate</strong><span>${result.tileCount} tiles</span><span>${result.exceedsLimit ? `exceeds max ${result.maxTileCount}` : `zoom ${result.minZoom}-${result.maxZoom}`}</span></div>` + target.innerHTML;
  }
}

async function startMapCacheDownload(): Promise<void> {
  const useAtStartup = document.querySelector<HTMLInputElement>('#map-cache-startup-default')?.checked === true;
  const result = await window.altairAnimus?.startMapCacheDownload?.(mapCacheForm());
  if (!result) return;
  if (!result.ok && result.error) document.querySelector<HTMLElement>('#status')!.textContent = result.error;
  if (result.ok && useAtStartup && result.status.downloadState?.setId) saveUiSettings({ activeMapCacheSetId: result.status.downloadState.setId });
  applyMapCacheStatus(result.status);
}

function demCacheForm() {
  const template = document.querySelector<HTMLInputElement>('#dem-cache-template')?.value.trim() ?? '';
  const attribution = document.querySelector<HTMLInputElement>('#dem-cache-attribution')?.value.trim() ?? '';
  const defaultProvider = document.querySelector<HTMLSelectElement>('#dem-cache-provider')?.value === DEM_PROVIDER_DEFAULT;
  const label = document.querySelector<HTMLInputElement>('#dem-cache-label')?.value.trim() || (defaultProvider ? 'Altair default: AWS Terrarium DEM' : '');
  const encodingValue = document.querySelector<HTMLSelectElement>('#dem-cache-encoding')?.value;
  const encoding: 'terrarium' | 'mapbox' = encodingValue === 'mapbox' ? 'mapbox' : 'terrarium';
  const minZoom = Number(document.querySelector<HTMLInputElement>('#dem-cache-min-zoom')?.value ?? ANIMUS_MAP_CACHE_DEFAULT_MIN_ZOOM);
  const maxZoom = Number(document.querySelector<HTMLInputElement>('#dem-cache-max-zoom')?.value ?? ANIMUS_MAP_CACHE_DEFAULT_MAX_ZOOM);
  const maxTileCount = Number(document.querySelector<HTMLInputElement>('#dem-cache-max-tiles')?.value ?? ANIMUS_MAP_CACHE_DEFAULT_MAX_TILE_COUNT);
  saveUiSettings({ demTileUrlTemplate: template, demTileAttribution: attribution, demTileEncoding: encoding, demCacheMinZoom: minZoom, demCacheMaxZoom: maxZoom, demCacheMaxTileCount: maxTileCount });
  return { urlTemplate: template, attribution, label, encoding, bbox: currentMapCacheBbox(state.snapshot), minZoom, maxZoom, maxTileCount };
}

async function estimateDemCache(): Promise<void> {
  const request = demCacheForm();
  const result = await window.altairAnimus?.estimateDemCache?.(request);
  if (!result) return;
  const target = document.querySelector<HTMLElement>('#setup-dem-cache-status');
  if (target) {
    target.innerHTML = `<div><strong>Estimate</strong><span>${result.tileCount} tiles</span><span>${result.exceedsLimit ? `exceeds max ${result.maxTileCount}` : `zoom ${result.minZoom}-${result.maxZoom}`}</span></div>` + target.innerHTML;
  }
}

async function startDemCacheDownload(): Promise<void> {
  const useAtStartup = document.querySelector<HTMLInputElement>('#dem-cache-startup-default')?.checked === true;
  const result = await window.altairAnimus?.startDemCacheDownload?.(demCacheForm());
  if (!result) return;
  if (!result.ok && result.error) document.querySelector<HTMLElement>('#status')!.textContent = result.error;
  if (result.ok && useAtStartup && result.status.downloadState?.setId) saveUiSettings({ activeDemCacheSetId: result.status.downloadState.setId });
  applyDemCacheStatus(result.status);
}

function formatBytes(bytes: number): string {
  if (bytes < 1024) return `${bytes} B`;
  if (bytes < 1024 * 1024) return `${(bytes / 1024).toFixed(1)} KiB`;
  return `${(bytes / (1024 * 1024)).toFixed(1)} MiB`;
}

setHudMode('console');
setTheme('grid', false);
setCameraLocked(false, false);
document.querySelector<HTMLCanvasElement>('#ortho')!.classList.toggle('hidden', !scene.ortho);
setWorkspace('flight', false);
populateMapCacheInputs(state.settings);
dashboard.load();
void window.altairAnimus?.getSettings?.().then(applyUiSettings).catch(() => {
  state.settingsLoaded = true;
});
void (window.altairAnimus?.getMapCacheStatus?.() ?? refreshMapCacheStatus()).then(applyMapCacheStatus).catch(() => {
  renderSetupMapCacheStatus(createUnavailableMapCacheStatus('map cache status unavailable'));
});
void window.altairAnimus?.getDemCacheStatus?.().then(applyDemCacheStatus).catch(() => {
  renderSetupDemCacheStatus(createUnavailableMapCacheStatus('DEM cache status unavailable'));
});
connect();
scene.start();
