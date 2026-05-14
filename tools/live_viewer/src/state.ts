export type VehicleStateMessage = {
  type: 'vehicle_state';
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
