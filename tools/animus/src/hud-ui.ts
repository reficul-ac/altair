import { headingDegFromYaw, yawDegFromRad, type SessionSnapshotMessage, type VehicleStateMessage } from './state';

export type HudMode = 'console' | 'tactical' | 'off';
export type DisplayDirectionSource = 'track' | 'heading' | 'yaw';
export type DisplayDirection = {
  degrees: number;
  source: DisplayDirectionSource;
  label: string;
};

const MIN_TRACK_SPEED_MPS = 0.1;

export function fmt(value: number | null | undefined, suffix = '', digits = 1): string {
  return value === null || value === undefined || Number.isNaN(value) ? '--' : `${value.toFixed(digits)}${suffix}`;
}

export function normalizeCompassDegrees(degrees: number): number {
  return ((degrees % 360) + 360) % 360;
}

export function groundTrackDegreesFromVelocity(velocity: Pick<VehicleStateMessage['velocity'], 'northMps' | 'eastMps'> | null | undefined): number | null {
  if (!velocity) return null;
  const north = velocity.northMps;
  const east = velocity.eastMps;
  if (!Number.isFinite(north) || !Number.isFinite(east)) return null;
  if (Math.hypot(north, east) < MIN_TRACK_SPEED_MPS) return null;
  return normalizeCompassDegrees((Math.atan2(east, north) * 180) / Math.PI);
}

export function displayDirectionFromVehicle(message: VehicleStateMessage): DisplayDirection {
  const track = groundTrackDegreesFromVelocity(message.velocity);
  if (track !== null) return { degrees: track, source: 'track', label: 'TRK' };
  if (message.metrics.headingDeg !== null && message.metrics.headingDeg !== undefined && Number.isFinite(message.metrics.headingDeg)) {
    return { degrees: normalizeCompassDegrees(message.metrics.headingDeg), source: 'heading', label: 'HDG' };
  }
  return { degrees: headingDegFromYaw(message.attitude.yawRad), source: 'yaw', label: 'YAW' };
}

function setText(id: string, value: string): void {
  document.querySelector<HTMLElement>(`#${id}`)!.textContent = value;
}

function levelForAge(ageS: number | null | undefined, warn: number, error: number): 'ok' | 'warning' | 'error' | 'stale' {
  if (ageS === null || ageS === undefined) return 'stale';
  if (ageS >= error) return 'error';
  if (ageS >= warn) return 'warning';
  return 'ok';
}

export function setHudMode(mode: HudMode): void {
  document.querySelectorAll<HTMLButtonElement>('[data-hud]').forEach((button) => {
    button.classList.toggle('active', button.dataset.hud === mode);
  });
  document.querySelector<HTMLElement>('#hud-console')!.classList.toggle('hidden', mode !== 'console');
  document.querySelector<HTMLElement>('#hud-tactical')!.classList.toggle('hidden', mode !== 'tactical');
}

export function updateHud(message: VehicleStateMessage, showYaw: boolean): void {
  const direction = displayDirectionFromVehicle(message);
  const heading = showYaw ? yawDegFromRad(message.attitude.yawRad) : direction.degrees;
  setText('heading-label', showYaw ? 'YAW' : direction.label);
  setText('heading', fmt(heading, ' deg', 0));
  setText('compass-bearing', `${direction.degrees.toFixed(0).padStart(3, '0')} deg`);
  setText('compass-source', direction.label);
  setText('roll', fmt((message.attitude.rollRad * 180) / Math.PI, ' deg', 0));
  setText('pitch', fmt((message.attitude.pitchRad * 180) / Math.PI, ' deg', 0));
  setText('altitude', fmt(message.globalPosition.altitudeM, ' m'));
  setText('airspeed', fmt(message.metrics.airspeedMps, ' m/s'));
  setText('groundspeed', fmt(message.metrics.groundspeedMps, ' m/s'));
  setText('climb', fmt(message.metrics.climbMps, ' m/s'));
  setText('tactical-gs', fmt(message.metrics.groundspeedMps, ' m/s'));
  setText('tactical-alt', fmt(message.globalPosition.altitudeM, ' m'));
  setText('heartbeat', fmt(message.heartbeatAgeS, ' s'));
  setText('packet', fmt(message.packetAgeS, ' s'));
  setText('throttle', fmt(message.metrics.throttlePct, '%', 0));
  setText('latlon', `${fmt(message.globalPosition.latDeg, '', 6)} / ${fmt(message.globalPosition.lonDeg, '', 6)}`);
  setText('position', `${fmt(message.localPosition.northM, ' N')} / ${fmt(message.localPosition.eastM, ' E')} / ${fmt(message.localPosition.upM, ' U')}`);
  setText('velocity', `${fmt(message.velocity.northMps, ' N')} / ${fmt(message.velocity.eastMps, ' E')} / ${fmt(-message.velocity.downMps, ' U')}`);
  setText('vehicle-type', message.vehicleType ?? 'Unknown vehicle');
  setText('vehicle-id', message.id ?? `sys ${message.systemId ?? '--'} comp ${message.componentId ?? '--'}`);
  setText('armed', message.status?.armingState?.label ?? (message.status?.armed === null || message.status?.armed === undefined ? '--' : message.status.armed ? 'Armed' : 'Disarmed'));
  setText('mode', message.status?.modeState?.label ?? message.status?.mode ?? '--');
  setText('gps', `${message.status?.gpsFix ?? '--'} / ${fmt(message.status?.satellitesVisible, ' sats', 0)}`);
  setText('battery', `${fmt(message.status?.batteryVoltageV, ' V', 2)} / ${fmt(message.status?.batteryRemainingPct, '%', 0)}`);
  setText('mission', message.status?.missionState
    ? `${message.status.missionState.state} ${fmt(message.status.missionState.activeSeq, '', 0)}`
    : fmt(message.status?.missionSeq, '', 0));
  setText('statustext', message.status?.lastStatusText ?? '--');

  const status = document.querySelector<HTMLElement>('#status')!;
  status.textContent = message.connected ? `Connected ${message.vehicleType ?? 'MAVLink'} ${message.id ?? ''}` : 'Waiting for MAVLink';
  status.className = `status ${message.connected ? 'online' : ''}`;
  document.querySelector<HTMLElement>('#attitude-bank')!.style.transform = `rotate(${message.attitude.rollRad}rad)`;
  const compass = document.querySelector<HTMLElement>('#flight-compass')!;
  compass.style.setProperty('--compass-bearing', direction.degrees.toFixed(2));
  compass.dataset.source = direction.source;
}

export function updateStatusStrip(message: VehicleStateMessage | null): void {
  const items = [
    ['link', message?.connected ? 'Link ok' : 'No link', message?.connected ? 'ok' : 'error'],
    ['packet-age', `Packet ${fmt(message?.packetAgeS, 's')}`, levelForAge(message?.packetAgeS, 0.8, 2)],
    ['heartbeat-age', `Heartbeat ${fmt(message?.heartbeatAgeS, 's')}`, levelForAge(message?.heartbeatAgeS, 2, 5)],
    ['vehicle-kind', message?.vehicleType ?? '--', message?.vehicleType ? 'ok' : 'stale'],
    ['authority-state', message?.commandCapabilities?.authority ?? 'unknown', message?.commandCapabilities?.blockedReason ? 'warning' : 'ok'],
    ['arm-state', message?.status?.armed ? 'Armed' : message?.status?.armed === false ? 'Disarmed' : '--', message?.status?.armed ? 'warning' : 'ok'],
    ['mode-state', message?.status?.modeState?.label ?? message?.status?.mode ?? '--', message?.status?.modeState?.known === false ? 'warning' : 'ok'],
    ['failsafe-state', message?.status?.failsafeState?.label ?? '--', message?.status?.failsafeState?.commandBlocking ? 'error' : message?.status?.failsafeState?.known === false ? 'warning' : 'ok'],
    ['mission-state', message?.status?.missionState?.detail ?? '--', message?.status?.missionState?.valid === false ? 'error' : message?.status?.missionState?.state === 'unknown' ? 'warning' : 'ok'],
    ['gps-state', message?.status?.gpsFix ?? '--', message?.status?.gpsFix?.includes('3D') ? 'ok' : 'warning'],
    ['battery-state', `${fmt(message?.status?.batteryVoltageV, 'V', 2)} ${fmt(message?.status?.batteryRemainingPct, '%', 0)}`, message?.status?.batteryRemainingPct !== null && message?.status?.batteryRemainingPct !== undefined && message.status.batteryRemainingPct < 20 ? 'warning' : 'ok']
  ];
  const strip = document.querySelector<HTMLElement>('#status-strip')!;
  strip.innerHTML = items.map(([key, label, level]) => `<button type="button" data-detail="${key}" class="${level}">${label}</button>`).join('');
}

export function updateVehicleList(snapshot: SessionSnapshotMessage, onSelect: (id: string) => void): void {
  const list = document.querySelector<HTMLElement>('#vehicle-list')!;
  list.innerHTML = snapshot.vehicles.map((vehicle) => {
    const id = vehicle.id ?? `${vehicle.systemId}:${vehicle.componentId}`;
    const active = id === snapshot.selectedVehicleId ? 'active' : '';
    return `<button type="button" class="${active}" data-vehicle-id="${id}"><strong>${id}</strong><span>${vehicle.vehicleType ?? 'MAVLink'} ${vehicle.connected ? 'online' : 'stale'}</span></button>`;
  }).join('') || '<p class="empty">No vehicles</p>';
  list.querySelectorAll<HTMLButtonElement>('[data-vehicle-id]').forEach((button) => {
    button.addEventListener('click', () => onSelect(button.dataset.vehicleId ?? ''));
  });
}
