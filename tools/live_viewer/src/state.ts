export type VehicleStateMessage = {
  type: 'vehicle_state';
  id?: string;
  connected: boolean;
  packetAgeS: number | null;
  heartbeatAgeS: number | null;
  systemId: number | null;
  componentId: number | null;
  vehicleType: string | null;
  attitude: {
    rollRad: number;
    pitchRad: number;
    yawRad: number;
    rollRateRps: number;
    pitchRateRps: number;
    yawRateRps: number;
  };
  globalPosition: {
    latDeg: number | null;
    lonDeg: number | null;
    altitudeM: number | null;
    relativeAltitudeM: number | null;
    originLatDeg: number | null;
    originLonDeg: number | null;
    originAltitudeM: number | null;
  };
  localPosition: {
    northM: number | null;
    eastM: number | null;
    upM: number | null;
  };
  velocity: {
    northMps: number;
    eastMps: number;
    downMps: number;
  };
  metrics: {
    headingDeg: number | null;
    airspeedMps: number | null;
    groundspeedMps: number | null;
    climbMps: number | null;
    throttlePct: number | null;
  };
  status?: {
    armed: boolean | null;
    mode: string | null;
    baseMode: number | null;
    customMode: number | null;
    gpsFix: string | null;
    satellitesVisible: number | null;
    batteryRemainingPct: number | null;
    batteryVoltageV: number | null;
    onboardControlSensorsHealth: number | null;
    missionSeq: number | null;
    lastStatusText: string | null;
  };
  home?: {
    latDeg: number;
    lonDeg: number;
    altitudeM: number;
  };
  mission?: {
    activeSeq: number | null;
    state?: MissionState;
    waypoints?: {
      seq: number;
      latDeg: number;
      lonDeg: number;
      altitudeM: number | null;
      command?: string | null;
      frame?: string | null;
    }[];
  };
  geofences?: GeofenceZone[];
  rallyPoints?: RallyPoint[];
  cameraStreams?: CameraStream[];
  commandCapabilities?: CommandCapabilityState;
  parameters?: ParameterValue[];
  diagnostics?: LinkDiagnostics;
  logSource?: LogSourceMetadata;
  trail?: (TrailPoint | { eastM: number; northM: number; upM: number; timestampS: number })[];
};

export type InspectorMessage = {
  key: string;
  msgId: number;
  name: string;
  systemId: number;
  componentId: number;
  lastAgeS: number;
  rateHz: number;
  count: number;
  fields: Record<string, number | string | null>;
};

export type SessionEvent = {
  id: string;
  timestampS: number;
  vehicleId: string | null;
  level: 'info' | 'warning' | 'error';
  kind: string;
  label: string;
  position: { eastM: number; northM: number; upM: number } | null;
};

export type SessionSnapshotMessage = {
  type: 'session_snapshot';
  vehicles: VehicleStateMessage[];
  selectedVehicleId: string | null;
  messages: InspectorMessage[];
  events: SessionEvent[];
  packetCount: number;
  decodedCount: number;
  analysis?: MultiVehicleAnalysis;
  logSources?: LogSourceMetadata[];
  console?: ConsoleLogEntry[];
  mockLinks?: MockLinkState[];
};

export type ReplaySourceType = 'altair-session' | 'ulog-import' | 'mavlink-live' | 'mock-link' | 'csv-import';

export type ReplayMetadata = {
  schemaVersion: number;
  sourceType: ReplaySourceType;
  createdAt: string;
  vehicleIds: string[];
  frameCount: number;
  packetCount: number;
  durationS: number;
  label?: string;
  importedFrom?: string;
  firmware?: string | null;
  vehicleName?: string | null;
  startedAt?: string | null;
};

export type ReplayTimelineMessage = {
  type: 'replay_timeline';
  loaded: boolean;
  playing: boolean;
  timestampS: number;
  durationS: number;
  speed: number;
  frameIndex: number;
  frameCount: number;
  markers: SessionEvent[];
  metadata: ReplayMetadata | null;
};

export type TrailPoint = {
  eastM: number;
  northM: number;
  upM: number;
  timestampMs: number;
};

export type MissionState = {
  activeSeq: number | null;
  totalItems: number;
  state: 'unknown' | 'not-started' | 'active' | 'paused' | 'complete';
  progressPct: number | null;
};

export type RallyPoint = {
  id: string;
  latDeg: number;
  lonDeg: number;
  altitudeM: number | null;
};

export type GeofenceZone =
  | {
      id: string;
      kind: 'polygon';
      inclusion: boolean;
      vertices: { latDeg: number; lonDeg: number }[];
      minAltitudeM?: number | null;
      maxAltitudeM?: number | null;
    }
  | {
      id: string;
      kind: 'circle';
      inclusion: boolean;
      center: { latDeg: number; lonDeg: number };
      radiusM: number;
      minAltitudeM?: number | null;
      maxAltitudeM?: number | null;
    };

export type CameraStream = {
  id: string;
  label: string;
  kind: 'rtp' | 'rtsp' | 'uvc' | 'mock';
  uri: string | null;
  status: 'offline' | 'available' | 'displaying' | 'recording' | 'error';
  error?: string | null;
  captureSupported: boolean;
  recordingSupported: boolean;
  telemetrySubtitleSupported: boolean;
};

export type CommandName =
  | 'arm'
  | 'disarm'
  | 'emergency-stop'
  | 'takeoff'
  | 'land'
  | 'return-to-launch'
  | 'pause'
  | 'change-altitude'
  | 'go-to'
  | 'orbit'
  | 'mission-start'
  | 'mission-continue'
  | 'mission-resume';

export type CommandCapabilityState = {
  liveLink: boolean;
  writableLink: boolean;
  supported: CommandName[];
  blockedReason: string | null;
};

export type GuardedCommandRequest = {
  command: CommandName;
  vehicleId: string;
  confirmed: boolean;
  params?: Record<string, number | string | boolean | null>;
};

export type GuardedCommandResult = {
  accepted: boolean;
  command: CommandName;
  vehicleId: string;
  reason: string;
  mock: boolean;
};

export type ParameterValue = {
  name: string;
  value: number | string;
  type: 'int' | 'float' | 'string' | 'bool';
  readonly: boolean;
  defaultValue?: number | string | null;
  min?: number | null;
  max?: number | null;
  description?: string | null;
};

export type LinkDiagnostics = {
  linkId: string;
  transport: 'udp' | 'tcp' | 'serial' | 'mock' | 'replay';
  status: 'offline' | 'listening' | 'connected' | 'degraded';
  packetsRx: number;
  packetsTx: number;
  decodedRx: number;
  drops: number;
  lastError: string | null;
};

export type LogSourceMetadata = {
  id: string;
  kind: ReplaySourceType;
  label: string;
  path?: string | null;
  vehicleId?: string | null;
  firmware?: string | null;
  startedAt?: string | null;
  importedAt: string;
};

export type ConsoleLogEntry = {
  timestampS: number;
  vehicleId: string | null;
  level: 'debug' | 'info' | 'warning' | 'error';
  source: string;
  message: string;
};

export type MockLinkState = {
  id: string;
  label: string;
  vehicleCount: number;
  running: boolean;
  diagnostics: LinkDiagnostics;
};

export type MultiVehicleAnalysis = {
  targetVehicleCount: number;
  alignment: 'takeoff' | 'boot' | 'absolute';
  referenceVehicleId: string | null;
  ghostTimestampS: number | null;
  deconfliction: {
    minimumSeparationM: number | null;
    conflicts: { a: string; b: string; distanceM: number; timestampS: number | null }[];
  };
  formation: {
    centroid: { eastM: number; northM: number; upM: number } | null;
    vehicles: { id: string; eastOffsetM: number; northOffsetM: number; upOffsetM: number }[];
  };
};

export const LIVE_VIEWER_TARGET_ANALYSIS_VEHICLES = 12;

export class TrailBuffer {
  private readonly points: TrailPoint[] = [];

  constructor(private readonly limit = 1200) {}

  add(point: TrailPoint): void {
    this.points.push(point);
    if (this.points.length > this.limit) {
      this.points.splice(0, this.points.length - this.limit);
    }
  }

  clear(): void {
    this.points.length = 0;
  }

  values(): readonly TrailPoint[] {
    return this.points;
  }
}

export function parseVehicleState(raw: string): VehicleStateMessage | null {
  let parsed: unknown;
  try {
    parsed = JSON.parse(raw);
  } catch {
    return null;
  }
  if (!parsed || typeof parsed !== 'object' || (parsed as { type?: unknown }).type !== 'vehicle_state') {
    return null;
  }
  return parsed as VehicleStateMessage;
}

export function parseSessionSnapshot(raw: string): SessionSnapshotMessage | null {
  let parsed: unknown;
  try {
    parsed = JSON.parse(raw);
  } catch {
    return null;
  }
  if (!parsed || typeof parsed !== 'object' || (parsed as { type?: unknown }).type !== 'session_snapshot') {
    return null;
  }
  return parsed as SessionSnapshotMessage;
}

export function trailPointFromState(state: VehicleStateMessage, timestampMs = Date.now()): TrailPoint | null {
  const { eastM, northM, upM } = state.localPosition;
  if (eastM === null || northM === null || upM === null) {
    return null;
  }
  return { eastM, northM, upM, timestampMs };
}

export function isTelemetryStale(state: VehicleStateMessage | null, staleAfterS = 2): boolean {
  if (!state?.connected || state.packetAgeS === null) {
    return true;
  }
  return state.packetAgeS >= staleAfterS;
}

export function hasRenderablePosition(state: VehicleStateMessage | null): boolean {
  return state !== null && trailPointFromState(state) !== null;
}

export function headingDegFromYaw(yawRad: number): number {
  const deg = (yawRad * 180) / Math.PI;
  return (90 - deg + 360) % 360;
}

export function yawDegFromRad(yawRad: number): number {
  const deg = (yawRad * 180) / Math.PI;
  return ((((deg + 180) % 360) + 360) % 360) - 180;
}
