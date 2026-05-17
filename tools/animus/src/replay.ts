import type { ReplayMetadata, ReplayTimelineMessage, SessionEvent, SessionSnapshotMessage } from './state.js';
import { assertCompatibleSessionSnapshot } from './session-compat.js';

export const ALTAIR_REPLAY_SCHEMA_VERSION = 1;

export type ReplayFrame = {
  timestampS: number;
  snapshot: SessionSnapshotMessage;
};

export type AltairReplayFile = {
  type: 'altair_session_replay';
  schemaVersion: typeof ALTAIR_REPLAY_SCHEMA_VERSION;
  metadata?: Partial<Omit<ReplayMetadata, 'schemaVersion' | 'vehicleIds' | 'frameCount' | 'packetCount' | 'durationS'>> & {
    vehicleIds?: string[];
    packetCount?: number;
    durationS?: number;
  };
  frames: ReplayFrame[];
  markers?: SessionEvent[];
};

export type NormalizedAltairReplayFile = AltairReplayFile & {
  metadata: ReplayMetadata;
  markers: SessionEvent[];
  compatibilityWarnings: string[];
};

export type ImportedLog = {
  name: string;
  sourceType: ReplayMetadata['sourceType'];
  text: string;
  importedAt?: string;
};

export type ReplayPlaybackState = ReplayTimelineMessage;

type ReplayEvents = {
  'session-snapshot': SessionSnapshotMessage;
  state: ReplayPlaybackState;
};

export class ReplaySession {
  private frames: ReplayFrame[] = [];
  private markers: SessionEvent[] = [];
  private metadata: ReplayMetadata | null = null;
  private timer: ReturnType<typeof setTimeout> | null = null;
  private playing = false;
  private speed = 1;
  private frameIndex = 0;
  private readonly listeners = new Map<keyof ReplayEvents, Set<(payload: ReplayEvents[keyof ReplayEvents]) => void>>();

  on<K extends keyof ReplayEvents>(event: K, callback: (payload: ReplayEvents[K]) => void): void {
    const callbacks = this.listeners.get(event) ?? new Set();
    callbacks.add(callback as (payload: ReplayEvents[keyof ReplayEvents]) => void);
    this.listeners.set(event, callbacks);
  }

  load(replay: AltairReplayFile): ReplayPlaybackState {
    this.stopTimer();
    const parsed = normalizeReplay(replay);
    this.frames = parsed.frames;
    this.markers = parsed.markers;
    this.metadata = parsed.metadata;
    this.playing = false;
    this.speed = 1;
    this.frameIndex = 0;
    this.emitState();
    this.emitCurrentFrame();
    return this.state();
  }

  state(): ReplayPlaybackState {
    const frame = this.frames[this.frameIndex] ?? null;
    return {
      type: 'replay_timeline',
      loaded: this.frames.length > 0,
      playing: this.playing,
      timestampS: frame?.timestampS ?? 0,
      durationS: this.metadata?.durationS ?? 0,
      speed: this.speed,
      frameIndex: this.frameIndex,
      frameCount: this.frames.length,
      markers: this.markers,
      metadata: this.metadata
    };
  }

  isLoaded(): boolean {
    return this.frames.length > 0;
  }

  exportReplay(): AltairReplayFile | null {
    if (this.frames.length === 0 || this.metadata === null) return null;
    return serializeAltairReplay(this.frames, this.metadata, this.markers);
  }

  play(): ReplayPlaybackState {
    if (this.frames.length === 0) return this.state();
    this.playing = true;
    this.scheduleNextFrame();
    this.emitState();
    return this.state();
  }

  pause(): ReplayPlaybackState {
    this.playing = false;
    this.stopTimer();
    this.emitState();
    return this.state();
  }

  setSpeed(speed: number): ReplayPlaybackState {
    if (!Number.isFinite(speed) || speed <= 0) {
      throw new Error('replay speed must be positive');
    }
    this.speed = Math.min(16, Math.max(0.1, speed));
    if (this.playing) this.scheduleNextFrame();
    this.emitState();
    return this.state();
  }

  reset(): ReplayPlaybackState {
    return this.seek(0, false);
  }

  selectVehicle(id: string): SessionSnapshotMessage | null {
    if (this.frames.length === 0) return null;
    this.frames = this.frames.map((frame) => {
      const selectedVehicleId = frame.snapshot.vehicles.some((vehicle) => (vehicle.id ?? `${vehicle.systemId}:${vehicle.componentId}`) === id)
        ? id
        : frame.snapshot.selectedVehicleId;
      return { ...frame, snapshot: { ...frame.snapshot, selectedVehicleId } };
    });
    this.emitCurrentFrame();
    return this.frames[this.frameIndex].snapshot;
  }

  seek(timestampS: number, keepPlaying = this.playing): ReplayPlaybackState {
    if (this.frames.length === 0) return this.state();
    const target = Math.min(this.durationS(), Math.max(0, timestampS));
    this.frameIndex = frameIndexAtOrBefore(this.frames, target);
    this.playing = keepPlaying;
    if (this.playing) this.scheduleNextFrame();
    else this.stopTimer();
    this.emitState();
    this.emitCurrentFrame();
    return this.state();
  }

  seekMarker(direction: -1 | 1): ReplayPlaybackState {
    if (this.markers.length === 0) return this.state();
    const now = this.state().timestampS;
    const ordered = [...this.markers].sort((a, b) => a.timestampS - b.timestampS);
    const marker = direction > 0
      ? ordered.find((event) => event.timestampS > now)
      : [...ordered].reverse().find((event) => event.timestampS < now);
    if (!marker) return this.state();
    return this.seek(marker.timestampS);
  }

  private scheduleNextFrame(): void {
    this.stopTimer();
    if (!this.playing || this.frames.length === 0) return;
    if (this.frameIndex >= this.frames.length - 1) {
      this.playing = false;
      this.emitState();
      return;
    }
    const current = this.frames[this.frameIndex];
    const next = this.frames[this.frameIndex + 1];
    const delayMs = Math.max(0, ((next.timestampS - current.timestampS) * 1000) / this.speed);
    this.timer = setTimeout(() => {
      this.frameIndex += 1;
      this.emitState();
      this.emitCurrentFrame();
      this.scheduleNextFrame();
    }, delayMs);
  }

  private emitCurrentFrame(): void {
    const frame = this.frames[this.frameIndex];
    if (frame) this.emit('session-snapshot', frame.snapshot);
  }

  private emitState(): void {
    this.emit('state', this.state());
  }

  private emit<K extends keyof ReplayEvents>(event: K, payload: ReplayEvents[K]): void {
    for (const callback of this.listeners.get(event) ?? []) {
      callback(payload);
    }
  }

  private stopTimer(): void {
    if (this.timer) {
      clearTimeout(this.timer);
      this.timer = null;
    }
  }

  private durationS(): number {
    return this.metadata?.durationS ?? this.frames.at(-1)?.timestampS ?? 0;
  }
}

export function parseAltairReplayJson(raw: string): NormalizedAltairReplayFile {
  let parsed: unknown;
  try {
    parsed = JSON.parse(raw);
  } catch (error) {
    throw new Error(`invalid replay JSON: ${error instanceof Error ? error.message : String(error)}`);
  }
  if (!parsed || typeof parsed !== 'object') {
    throw new Error('replay file must be an object');
  }
  return normalizeReplay(parsed as AltairReplayFile);
}

export function importLogAsReplay(log: ImportedLog): AltairReplayFile {
  const trimmed = log.text.trim();
  if (trimmed.startsWith('{')) {
    const replay = parseAltairReplayJson(trimmed);
    const normalized = normalizeReplay(replay);
    return serializeAltairReplay(replay.frames, {
      ...normalized.metadata,
      sourceType: log.sourceType,
      importedFrom: log.name,
      createdAt: log.importedAt ?? normalized.metadata.createdAt
    }, normalized.markers);
  }
  return importDelimitedLogAsReplay(log);
}

export function serializeAltairReplay(frames: readonly ReplayFrame[], metadata: Partial<ReplayMetadata> = {}, markers: readonly SessionEvent[] = []): AltairReplayFile {
  const normalized = normalizeFrames(frames).frames;
  const fallbackMarkers = markerEvents(normalized);
  return {
    type: 'altair_session_replay',
    schemaVersion: ALTAIR_REPLAY_SCHEMA_VERSION,
    metadata: {
      sourceType: metadata.sourceType ?? 'altair-session',
      createdAt: metadata.createdAt ?? new Date(0).toISOString(),
      label: metadata.label,
      importedFrom: metadata.importedFrom,
      firmware: metadata.firmware,
      vehicleName: metadata.vehicleName,
      startedAt: metadata.startedAt,
      vehicleIds: metadata.vehicleIds ?? vehicleIdsFor(normalized),
      packetCount: metadata.packetCount ?? normalized.at(-1)?.snapshot.packetCount ?? 0,
      durationS: metadata.durationS ?? normalized.at(-1)?.timestampS ?? 0
    },
    frames: normalized,
    markers: markers.length > 0 ? [...markers] : fallbackMarkers
  };
}

export function normalizeReplay(replay: AltairReplayFile): NormalizedAltairReplayFile {
  if (replay.type !== 'altair_session_replay') {
    throw new Error('unsupported replay type');
  }
  if (replay.schemaVersion !== ALTAIR_REPLAY_SCHEMA_VERSION) {
    throw new Error(`unsupported replay schema version ${String(replay.schemaVersion)}`);
  }
  const { frames, warnings } = normalizeFrames(replay.frames);
  if (frames.length === 0) {
    throw new Error('replay must contain at least one frame');
  }
  const metadata = buildMetadata(frames, replay.metadata);
  const markers = [...(replay.markers ?? markerEvents(frames))].sort((a, b) => a.timestampS - b.timestampS);
  return { ...replay, frames, markers, metadata, compatibilityWarnings: warnings };
}

export function frameIndexAtOrBefore(frames: readonly ReplayFrame[], timestampS: number): number {
  let low = 0;
  let high = frames.length - 1;
  let result = 0;
  while (low <= high) {
    const mid = Math.floor((low + high) / 2);
    if (frames[mid].timestampS <= timestampS) {
      result = mid;
      low = mid + 1;
    } else {
      high = mid - 1;
    }
  }
  return result;
}

function normalizeFrames(frames: readonly ReplayFrame[] | undefined): { frames: ReplayFrame[]; warnings: string[] } {
  if (!Array.isArray(frames)) {
    throw new Error('replay frames must be an array');
  }
  const warnings: string[] = [];
  const normalized = frames.map((frame, index) => {
    if (!frame || typeof frame !== 'object') {
      throw new Error(`replay frame ${index} must be an object`);
    }
    if (!Number.isFinite(frame.timestampS) || frame.timestampS < 0) {
      throw new Error(`replay frame ${index} has invalid timestamp`);
    }
    const snapshot = assertCompatibleSessionSnapshot(frame.snapshot, `replay frame ${index}`);
    warnings.push(...snapshot.warnings.map((warning) => `replay frame ${index}: ${warning}`));
    return { timestampS: frame.timestampS, snapshot: snapshot.snapshot };
  });
  normalized.sort((a, b) => a.timestampS - b.timestampS);
  return { frames: normalized, warnings };
}

function buildMetadata(frames: readonly ReplayFrame[], metadata: AltairReplayFile['metadata'] = {}): ReplayMetadata {
  return {
    schemaVersion: ALTAIR_REPLAY_SCHEMA_VERSION,
    sourceType: metadata.sourceType ?? 'altair-session',
    createdAt: metadata.createdAt ?? new Date(0).toISOString(),
    label: metadata.label,
    vehicleIds: metadata.vehicleIds ?? vehicleIdsFor(frames),
    frameCount: frames.length,
    packetCount: metadata.packetCount ?? frames.at(-1)?.snapshot.packetCount ?? 0,
    durationS: metadata.durationS ?? frames.at(-1)?.timestampS ?? 0,
    importedFrom: metadata.importedFrom,
    firmware: metadata.firmware,
    vehicleName: metadata.vehicleName,
    startedAt: metadata.startedAt
  };
}

function vehicleIdsFor(frames: readonly ReplayFrame[]): string[] {
  const ids = new Set<string>();
  for (const frame of frames) {
    for (const vehicle of frame.snapshot.vehicles) {
      ids.add(vehicle.id ?? `${vehicle.systemId ?? '--'}:${vehicle.componentId ?? '--'}`);
    }
  }
  return [...ids].sort();
}

function markerEvents(frames: readonly ReplayFrame[]): SessionEvent[] {
  const events = new Map<string, SessionEvent>();
  for (const frame of frames) {
    for (const event of frame.snapshot.events) {
      if (event.kind === 'marker') {
        events.set(event.id, event);
      }
    }
  }
  return [...events.values()].sort((a, b) => a.timestampS - b.timestampS);
}

function importDelimitedLogAsReplay(log: ImportedLog): AltairReplayFile {
  const rows = parseDelimitedRows(log.text);
  if (rows.length === 0) {
    throw new Error('imported log has no samples');
  }
  const frames = rows.map((row, index) => {
    const timestampS = numberField(row, ['timestamp_s', 'time_s', 't', 'TimeS']) ?? index;
    const vehicleId = stringField(row, ['vehicle_id', 'vehicle', 'id', 'Vehicle']) ?? '1:1';
    const [systemId, componentId] = vehicleId.split(':').map((part) => Number(part));
    const northM = numberField(row, ['north_m', 'north', 'x_north_m', 'pos_n_m']) ?? 0;
    const eastM = numberField(row, ['east_m', 'east', 'y_east_m', 'pos_e_m']) ?? 0;
    const upM = numberField(row, ['up_m', 'up', 'altitude_m', 'alt_m']) ?? 0;
    const snapshot: SessionSnapshotMessage = {
      type: 'session_snapshot',
      vehicles: [{
        type: 'vehicle_state',
        id: vehicleId,
        connected: true,
        packetAgeS: 0,
        heartbeatAgeS: 0,
        systemId: Number.isFinite(systemId) ? systemId : 1,
        componentId: Number.isFinite(componentId) ? componentId : 1,
        vehicleType: stringField(row, ['vehicle_type', 'type']) ?? 'Imported log',
        attitude: {
          rollRad: numberField(row, ['roll_rad', 'roll']) ?? 0,
          pitchRad: numberField(row, ['pitch_rad', 'pitch']) ?? 0,
          yawRad: numberField(row, ['yaw_rad', 'yaw']) ?? 0,
          rollRateRps: numberField(row, ['roll_rate_rps']) ?? 0,
          pitchRateRps: numberField(row, ['pitch_rate_rps']) ?? 0,
          yawRateRps: numberField(row, ['yaw_rate_rps']) ?? 0
        },
        globalPosition: {
          latDeg: numberField(row, ['lat_deg', 'lat']),
          lonDeg: numberField(row, ['lon_deg', 'lon']),
          altitudeM: numberField(row, ['altitude_m', 'alt_m']),
          relativeAltitudeM: numberField(row, ['relative_altitude_m', 'rel_alt_m']),
          originLatDeg: numberField(row, ['origin_lat_deg']),
          originLonDeg: numberField(row, ['origin_lon_deg']),
          originAltitudeM: numberField(row, ['origin_altitude_m'])
        },
        localPosition: { northM, eastM, upM },
        velocity: {
          northMps: numberField(row, ['vn_mps', 'vel_n_mps']) ?? 0,
          eastMps: numberField(row, ['ve_mps', 'vel_e_mps']) ?? 0,
          downMps: numberField(row, ['vd_mps', 'vel_d_mps']) ?? 0
        },
        metrics: {
          headingDeg: numberField(row, ['heading_deg', 'heading']),
          airspeedMps: numberField(row, ['airspeed_mps', 'airspeed']),
          groundspeedMps: numberField(row, ['groundspeed_mps', 'groundspeed']),
          climbMps: numberField(row, ['climb_mps', 'climb']),
          throttlePct: numberField(row, ['throttle_pct', 'throttle'])
        },
        status: {
          armed: boolField(row, ['armed']),
          mode: stringField(row, ['mode']),
          baseMode: null,
          customMode: null,
          gpsFix: stringField(row, ['gps_fix']),
          satellitesVisible: numberField(row, ['satellites_visible', 'satellites']),
          batteryRemainingPct: numberField(row, ['battery_remaining_pct', 'battery_pct']),
          batteryVoltageV: numberField(row, ['battery_voltage_v']),
          onboardControlSensorsHealth: null,
          missionSeq: numberField(row, ['mission_seq']),
          lastStatusText: stringField(row, ['status_text'])
        },
        logSource: {
          id: `log-${log.name}`,
          kind: log.sourceType,
          label: log.name,
          path: log.name,
          importedAt: log.importedAt ?? new Date(0).toISOString()
        }
      }],
      selectedVehicleId: vehicleId,
      messages: [],
      events: [],
      packetCount: index + 1,
      decodedCount: index + 1,
      logSources: [{
        id: `log-${log.name}`,
        kind: log.sourceType,
        label: log.name,
        path: log.name,
        importedAt: log.importedAt ?? new Date(0).toISOString()
      }]
    };
    return { timestampS, snapshot };
  });
  return serializeAltairReplay(frames, {
    sourceType: log.sourceType,
    createdAt: log.importedAt ?? new Date(0).toISOString(),
    importedFrom: log.name,
    label: log.name
  });
}

function parseDelimitedRows(text: string): Record<string, string>[] {
  const lines = text.split(/\r?\n/).map((line) => line.trim()).filter(Boolean);
  if (lines.length < 2) return [];
  const delimiter = lines[0].includes('\t') ? '\t' : ',';
  const headers = lines[0].split(delimiter).map((part) => part.trim());
  return lines.slice(1).map((line) => {
    const values = line.split(delimiter);
    return Object.fromEntries(headers.map((header, index) => [header, values[index]?.trim() ?? '']));
  });
}

function numberField(row: Record<string, string>, keys: string[]): number | null {
  for (const key of keys) {
    const value = Number(row[key]);
    if (Number.isFinite(value)) return value;
  }
  return null;
}

function stringField(row: Record<string, string>, keys: string[]): string | null {
  for (const key of keys) {
    const value = row[key];
    if (value !== undefined && value.length > 0) return value;
  }
  return null;
}

function boolField(row: Record<string, string>, keys: string[]): boolean | null {
  const value = stringField(row, keys);
  if (value === null) return null;
  return ['1', 'true', 'yes', 'armed'].includes(value.toLowerCase());
}
