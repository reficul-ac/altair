import type { ReplayMetadata, ReplayTimelineMessage, SessionEvent, SessionSnapshotMessage } from './state.js';

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

export function parseAltairReplayJson(raw: string): AltairReplayFile {
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

export function serializeAltairReplay(frames: readonly ReplayFrame[], metadata: Partial<ReplayMetadata> = {}, markers: readonly SessionEvent[] = []): AltairReplayFile {
  const normalized = normalizeFrames(frames);
  const fallbackMarkers = markerEvents(normalized);
  return {
    type: 'altair_session_replay',
    schemaVersion: ALTAIR_REPLAY_SCHEMA_VERSION,
    metadata: {
      sourceType: metadata.sourceType ?? 'altair-session',
      createdAt: metadata.createdAt ?? new Date(0).toISOString(),
      label: metadata.label,
      vehicleIds: metadata.vehicleIds ?? vehicleIdsFor(normalized),
      packetCount: metadata.packetCount ?? normalized.at(-1)?.snapshot.packetCount ?? 0,
      durationS: metadata.durationS ?? normalized.at(-1)?.timestampS ?? 0
    },
    frames: normalized,
    markers: markers.length > 0 ? [...markers] : fallbackMarkers
  };
}

export function normalizeReplay(replay: AltairReplayFile): AltairReplayFile & { metadata: ReplayMetadata; markers: SessionEvent[] } {
  if (replay.type !== 'altair_session_replay') {
    throw new Error('unsupported replay type');
  }
  if (replay.schemaVersion !== ALTAIR_REPLAY_SCHEMA_VERSION) {
    throw new Error(`unsupported replay schema version ${String(replay.schemaVersion)}`);
  }
  const frames = normalizeFrames(replay.frames);
  if (frames.length === 0) {
    throw new Error('replay must contain at least one frame');
  }
  const metadata = buildMetadata(frames, replay.metadata);
  const markers = [...(replay.markers ?? markerEvents(frames))].sort((a, b) => a.timestampS - b.timestampS);
  return { ...replay, frames, markers, metadata };
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

function normalizeFrames(frames: readonly ReplayFrame[] | undefined): ReplayFrame[] {
  if (!Array.isArray(frames)) {
    throw new Error('replay frames must be an array');
  }
  const normalized = frames.map((frame, index) => {
    if (!frame || typeof frame !== 'object') {
      throw new Error(`replay frame ${index} must be an object`);
    }
    if (!Number.isFinite(frame.timestampS) || frame.timestampS < 0) {
      throw new Error(`replay frame ${index} has invalid timestamp`);
    }
    if (!frame.snapshot || frame.snapshot.type !== 'session_snapshot') {
      throw new Error(`replay frame ${index} must contain a session snapshot`);
    }
    return { timestampS: frame.timestampS, snapshot: frame.snapshot };
  });
  normalized.sort((a, b) => a.timestampS - b.timestampS);
  return normalized;
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
    durationS: metadata.durationS ?? frames.at(-1)?.timestampS ?? 0
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
