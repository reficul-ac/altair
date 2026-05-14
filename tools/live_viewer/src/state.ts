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
    waypoints?: {
      seq: number;
      latDeg: number;
      lonDeg: number;
      altitudeM: number | null;
    }[];
  };
  trail?: TrailPoint[];
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
};

export type TrailPoint = {
  eastM: number;
  northM: number;
  upM: number;
  timestampMs: number;
};

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
