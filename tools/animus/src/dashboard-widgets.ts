import type { AnimusWidgetConfig, AnimusWidgetKind, AnimusWidgetRuntimeConfig, AnimusWidgetSpan, AnimusWidgetVehicleScope } from './dashboard-types';
import type { CommandName, SessionSnapshotMessage, VehicleStateMessage } from './state';

export type DashboardWidgetCatalogEntry = {
  kind: AnimusWidgetKind;
  label: string;
  detail: string;
  span: AnimusWidgetSpan;
  group: DashboardWidgetCatalogGroupId;
};

export type DashboardWidgetCatalogGroupId = 'flight-test' | 'mission-planning' | 'maintenance';

export type DashboardWidgetCatalogGroup = {
  id: DashboardWidgetCatalogGroupId;
  label: string;
  kinds: AnimusWidgetKind[];
};

export type DashboardWidgetRow = {
  label: string;
  value: string;
  tone?: 'ok' | 'warning' | 'danger' | 'muted';
};

export type DashboardWidgetView = {
  title: string;
  rows: DashboardWidgetRow[];
  empty?: string;
};

export const DASHBOARD_COMMANDS: CommandName[] = ['arm', 'disarm', 'pause', 'return-to-launch', 'land', 'takeoff', 'change-altitude'];

export const DASHBOARD_WIDGET_CATALOG: DashboardWidgetCatalogEntry[] = [
  { kind: 'link-freshness', label: 'Link / Packets', detail: 'Connection state, heartbeat age, packet age, and session counters.', span: 'compact', group: 'flight-test' },
  { kind: 'identity-mode', label: 'Identity / Mode', detail: 'Vehicle id, firmware, type, base mode, and normalized mode.', span: 'compact', group: 'maintenance' },
  { kind: 'arm-failsafe-readiness', label: 'Arm / Failsafe', detail: 'Arming state, failsafe state, readiness summary, and command authority.', span: 'compact', group: 'maintenance' },
  { kind: 'gps-battery', label: 'GPS / Battery', detail: 'GPS fix, satellites, battery voltage, and remaining percentage.', span: 'compact', group: 'mission-planning' },
  { kind: 'attitude-summary', label: 'Attitude', detail: 'Roll, pitch, yaw, heading, and attitude rates.', span: 'compact', group: 'flight-test' },
  { kind: 'position-velocity', label: 'Position / Velocity', detail: 'Global position, local position, airspeed, groundspeed, and climb.', span: 'compact', group: 'mission-planning' },
  { kind: 'mission-progress', label: 'Mission', detail: 'Active waypoint, progress, total items, and validation state.', span: 'compact', group: 'mission-planning' },
  { kind: 'status-events', label: 'Status / Events', detail: 'Latest status text and recent session events.', span: 'full', group: 'flight-test' },
  { kind: 'guarded-controls', label: 'Guarded Controls', detail: 'Existing guarded command workflow for selected vehicle actions.', span: 'full', group: 'maintenance' }
];

export const DASHBOARD_WIDGET_CATALOG_GROUPS: DashboardWidgetCatalogGroup[] = [
  { id: 'flight-test', label: 'Flight Test', kinds: ['link-freshness', 'attitude-summary', 'status-events'] },
  { id: 'mission-planning', label: 'Mission Planning', kinds: ['mission-progress', 'position-velocity', 'gps-battery'] },
  { id: 'maintenance', label: 'Maintenance', kinds: ['identity-mode', 'arm-failsafe-readiness', 'guarded-controls'] }
];

export function catalogEntry(kind: AnimusWidgetKind): DashboardWidgetCatalogEntry {
  return DASHBOARD_WIDGET_CATALOG.find((entry) => entry.kind === kind) ?? DASHBOARD_WIDGET_CATALOG[0];
}

export function selectedDashboardVehicle(snapshot: SessionSnapshotMessage | null): VehicleStateMessage | null {
  if (!snapshot) return null;
  return snapshot.vehicles.find((vehicle) => vehicle.id === snapshot.selectedVehicleId) ?? snapshot.vehicles[0] ?? null;
}

export function buildDashboardWidgetView(config: AnimusWidgetConfig, snapshot: SessionSnapshotMessage | null): DashboardWidgetView {
  const vehicles = scopedDashboardVehicles(config.config?.vehicleScope, snapshot);
  const vehicle = vehicles[0] ?? null;
  const entry = catalogEntry(config.kind);
  if (!vehicle && config.kind !== 'status-events') {
    return { title: entry.label, rows: [], empty: emptyVehicleText(config.config?.vehicleScope) };
  }

  switch (config.kind) {
    case 'link-freshness':
      if (config.config?.vehicleScope === 'fleet') {
        const packet = worstAge(vehicles.map((candidate) => candidate.packetAgeS));
        const heartbeat = worstAge(vehicles.map((candidate) => candidate.heartbeatAgeS));
        return {
          title: entry.label,
          rows: [
            { label: 'Fleet', value: `${vehicles.filter((candidate) => candidate.connected).length}/${vehicles.length} live`, tone: vehicles.every((candidate) => candidate.connected) ? 'ok' : 'danger' },
            { label: 'Worst packet', value: seconds(packet), tone: staleTone(packet, config.config) },
            { label: 'Worst heartbeat', value: seconds(heartbeat), tone: staleTone(heartbeat, config.config, 'heartbeat') },
            { label: 'Decoded', value: String(snapshot?.decodedCount ?? 0) },
            { label: 'Packets', value: String(snapshot?.packetCount ?? 0) }
          ],
          empty: vehicles.length > 0 ? undefined : 'No fleet vehicle streams'
        };
      }
      return {
        title: entry.label,
        rows: [
          { label: 'Link', value: vehicle?.connected ? 'live' : 'stale', tone: vehicle?.connected ? 'ok' : 'danger' },
          { label: 'Packet age', value: seconds(vehicle?.packetAgeS), tone: staleTone(vehicle?.packetAgeS, config.config) },
          { label: 'Heartbeat age', value: seconds(vehicle?.heartbeatAgeS), tone: staleTone(vehicle?.heartbeatAgeS, config.config, 'heartbeat') },
          { label: 'Decoded', value: String(snapshot?.decodedCount ?? 0) },
          { label: 'Packets', value: String(snapshot?.packetCount ?? 0) }
        ]
      };
    case 'identity-mode':
      return {
        title: entry.label,
        rows: [
          { label: 'Vehicle', value: vehicleLabel(vehicle) },
          { label: 'Type', value: vehicle?.vehicleType ?? '--' },
          { label: 'Firmware', value: vehicle?.status?.firmware?.label ?? 'unknown' },
          { label: 'Mode', value: vehicle?.status?.modeState?.label ?? vehicle?.status?.mode ?? '--' },
          { label: 'Base / custom', value: `${valueOrDash(vehicle?.status?.baseMode)} / ${valueOrDash(vehicle?.status?.customMode)}` }
        ]
      };
    case 'arm-failsafe-readiness':
      return {
        title: entry.label,
        rows: [
          { label: 'Arming', value: vehicle?.status?.armed === null || vehicle?.status?.armed === undefined ? '--' : vehicle.status.armed ? 'armed' : 'disarmed', tone: vehicle?.status?.armed ? 'warning' : 'ok' },
          { label: 'Failsafe', value: vehicle?.status?.failsafeState?.label ?? 'unknown', tone: vehicle?.status?.failsafeState?.status === 'active' ? 'danger' : 'muted' },
          { label: 'Readiness', value: vehicle?.status?.readiness?.overall ?? 'unknown' },
          { label: 'Authority', value: vehicle?.commandCapabilities?.authority ?? 'unknown' },
          { label: 'Guard', value: vehicle?.commandCapabilities?.blockedReason ?? 'commands available', tone: vehicle?.commandCapabilities?.blockedReason ? 'warning' : 'ok' }
        ]
      };
    case 'gps-battery':
      return {
        title: entry.label,
        rows: [
          { label: 'GPS', value: vehicle?.status?.gpsFix ?? '--' },
          { label: 'Satellites', value: valueOrDash(vehicle?.status?.satellitesVisible) },
          { label: 'Battery', value: percent(vehicle?.status?.batteryRemainingPct) },
          { label: 'Voltage', value: metersPerSecond(vehicle?.status?.batteryVoltageV, 'V') }
        ]
      };
    case 'attitude-summary':
      return {
        title: entry.label,
        rows: [
          { label: 'Roll', value: degrees(vehicle?.attitude.rollRad) },
          { label: 'Pitch', value: degrees(vehicle?.attitude.pitchRad) },
          { label: 'Yaw', value: degrees(vehicle?.attitude.yawRad) },
          { label: 'Heading', value: vehicle?.metrics.headingDeg === null || vehicle?.metrics.headingDeg === undefined ? '--' : `${vehicle.metrics.headingDeg.toFixed(0)} deg` },
          { label: 'Rates', value: `${vehicle?.attitude.rollRateRps.toFixed(2) ?? '--'} / ${vehicle?.attitude.pitchRateRps.toFixed(2) ?? '--'} / ${vehicle?.attitude.yawRateRps.toFixed(2) ?? '--'} rad/s` }
        ]
      };
    case 'position-velocity':
      return {
        title: entry.label,
        rows: [
          { label: 'Lat / Lon', value: vehicle?.globalPosition.latDeg === null || vehicle?.globalPosition.lonDeg === null ? '--' : `${vehicle?.globalPosition.latDeg?.toFixed(6)} / ${vehicle?.globalPosition.lonDeg?.toFixed(6)}` },
          { label: 'Altitude', value: meters(vehicle?.globalPosition.relativeAltitudeM ?? vehicle?.globalPosition.altitudeM) },
          { label: 'Local N/E/U', value: `${meters(vehicle?.localPosition.northM)} / ${meters(vehicle?.localPosition.eastM)} / ${meters(vehicle?.localPosition.upM)}` },
          { label: 'Velocity N/E/D', value: `${metersPerSecond(vehicle?.velocity.northMps)} / ${metersPerSecond(vehicle?.velocity.eastMps)} / ${metersPerSecond(vehicle?.velocity.downMps)}` },
          { label: 'Air / ground / climb', value: `${metersPerSecond(vehicle?.metrics.airspeedMps)} / ${metersPerSecond(vehicle?.metrics.groundspeedMps)} / ${metersPerSecond(vehicle?.metrics.climbMps)}` }
        ]
      };
    case 'mission-progress':
      return {
        title: entry.label,
        rows: [
          { label: 'State', value: vehicle?.status?.missionState?.state ?? vehicle?.mission?.state?.state ?? 'unknown' },
          { label: 'Progress', value: percent(vehicle?.status?.missionState?.progressPct ?? vehicle?.mission?.state?.progressPct) },
          { label: 'Active seq', value: valueOrDash(vehicle?.status?.missionSeq ?? vehicle?.mission?.activeSeq) },
          { label: 'Items', value: valueOrDash(vehicle?.status?.missionState?.totalItems ?? vehicle?.mission?.state?.totalItems ?? vehicle?.mission?.waypoints?.length) },
          { label: 'Detail', value: vehicle?.status?.missionState?.detail ?? vehicle?.mission?.state?.detail ?? '--' }
        ]
      };
    case 'status-events':
      return {
        title: entry.label,
        rows: [
          { label: 'Status text', value: vehicle?.status?.lastStatusText ?? '--' },
          ...(snapshot?.events ?? []).slice(0, 5).map((event) => ({ label: event.kind, value: event.label, tone: event.level === 'error' ? 'danger' as const : event.level === 'warning' ? 'warning' as const : 'muted' as const }))
        ],
        empty: snapshot?.events.length ? undefined : 'No recent events'
      };
    case 'guarded-controls':
      return {
        title: entry.label,
        rows: [
          { label: 'Authority', value: vehicle?.commandCapabilities?.authority ?? 'unknown' },
          { label: 'Writable', value: vehicle?.commandCapabilities?.writableLink ? 'yes' : 'no', tone: vehicle?.commandCapabilities?.writableLink ? 'ok' : 'warning' },
          { label: 'Guard', value: vehicle?.commandCapabilities?.blockedReason ?? 'commands available', tone: vehicle?.commandCapabilities?.blockedReason ? 'warning' : 'ok' }
        ]
      };
  }
}

export function renderDashboardWidgetRows(view: DashboardWidgetView): string {
  if (view.rows.length === 0) {
    return `<p class="empty">${escapeHtml(view.empty ?? 'No data')}</p>`;
  }
  return view.rows.map((row) => `<div class="${row.tone ? `tone-${row.tone}` : ''}"><span>${escapeHtml(row.label)}</span><strong>${escapeHtml(row.value)}</strong></div>`).join('');
}

export function scopedDashboardVehicles(scope: AnimusWidgetVehicleScope | undefined, snapshot: SessionSnapshotMessage | null): VehicleStateMessage[] {
  if (!snapshot) return [];
  if (scope === 'fleet') return snapshot.vehicles;
  const selected = selectedDashboardVehicle(snapshot);
  return selected ? [selected] : [];
}

export function dashboardCommandDisabledReason(vehicle: VehicleStateMessage | null, command: CommandName): string | null {
  if (!vehicle) return 'no selected vehicle';
  const capabilities = vehicle.commandCapabilities;
  if (!capabilities) return 'command capabilities have not been decoded';
  if (!capabilities.liveLink) return 'link is not live';
  if (capabilities.stale) return 'link is stale';
  if (!capabilities.writableLink) return capabilities.blockedReason ?? 'link is read-only';
  if (!capabilities.supported.includes(command)) return capabilities.blockedCommands?.[command] ?? capabilities.blockedReason ?? 'command is unsupported';
  return null;
}

export function escapeHtml(value: string): string {
  return value.replaceAll('&', '&amp;').replaceAll('<', '&lt;').replaceAll('>', '&gt;').replaceAll('"', '&quot;').replaceAll("'", '&#39;');
}

function vehicleLabel(vehicle: VehicleStateMessage | null): string {
  return vehicle?.id ?? `${valueOrDash(vehicle?.systemId)}:${valueOrDash(vehicle?.componentId)}`;
}

function staleTone(value: number | null | undefined, config?: AnimusWidgetRuntimeConfig, thresholdKind: 'packet' | 'heartbeat' = 'packet'): DashboardWidgetRow['tone'] {
  if (value === null || value === undefined) return 'muted';
  const warning = thresholdKind === 'heartbeat' ? config?.thresholds?.heartbeatWarningS ?? 1 : config?.thresholds?.packetWarningS ?? 1;
  const danger = thresholdKind === 'heartbeat' ? config?.thresholds?.heartbeatDangerS ?? 2 : config?.thresholds?.packetDangerS ?? 2;
  return value > danger ? 'danger' : value > warning ? 'warning' : 'ok';
}

function seconds(value: number | null | undefined): string {
  return value === null || value === undefined ? '--' : `${value.toFixed(2)} s`;
}

function meters(value: number | null | undefined): string {
  return value === null || value === undefined ? '--' : `${value.toFixed(1)} m`;
}

function metersPerSecond(value: number | null | undefined, suffix = 'm/s'): string {
  return value === null || value === undefined ? '--' : `${value.toFixed(1)} ${suffix}`;
}

function degrees(value: number | null | undefined): string {
  return value === null || value === undefined ? '--' : `${(value * 180 / Math.PI).toFixed(1)} deg`;
}

function percent(value: number | null | undefined): string {
  return value === null || value === undefined ? '--' : `${value.toFixed(0)}%`;
}

function valueOrDash(value: number | string | null | undefined): string {
  return value === null || value === undefined ? '--' : String(value);
}

function worstAge(values: Array<number | null | undefined>): number | null {
  const numeric = values.filter((value): value is number => typeof value === 'number' && Number.isFinite(value));
  return numeric.length > 0 ? Math.max(...numeric) : null;
}

function emptyVehicleText(scope: AnimusWidgetVehicleScope | undefined): string {
  return scope === 'fleet' ? 'No fleet vehicle streams' : 'No selected vehicle stream';
}
