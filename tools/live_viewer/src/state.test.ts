import { describe, expect, it } from 'vitest';
import {
  hasRenderablePosition,
  isTelemetryStale,
  headingDegFromYaw,
  parseVehicleState,
  trailPointFromState,
  TrailBuffer,
  yawDegFromRad,
  type VehicleStateMessage
} from './state';

const message: VehicleStateMessage = {
  type: 'vehicle_state',
  connected: true,
  packetAgeS: 0.1,
  heartbeatAgeS: 0.5,
  systemId: 1,
  componentId: 1,
  vehicleType: 'Fixed-wing',
  attitude: {
    rollRad: 0.1,
    pitchRad: 0.2,
    yawRad: 0.3,
    rollRateRps: 0,
    pitchRateRps: 0,
    yawRateRps: 0
  },
  globalPosition: {
    latDeg: 37,
    lonDeg: -122,
    altitudeM: 150,
    relativeAltitudeM: 150,
    originLatDeg: 37,
    originLonDeg: -122,
    originAltitudeM: 150
  },
  localPosition: { eastM: 2, northM: 3, upM: 4 },
  velocity: { northMps: 18, eastMps: 1, downMps: -0.2 },
  metrics: {
    headingDeg: 20,
    airspeedMps: 18.5,
    groundspeedMps: 18.1,
    climbMps: 0.2,
    throttlePct: 0
  }
};

describe('live viewer state', () => {
  it('parses vehicle state messages', () => {
    expect(parseVehicleState(JSON.stringify(message))?.metrics.airspeedMps).toBe(18.5);
    expect(parseVehicleState(JSON.stringify({ type: 'other' }))).toBeNull();
    expect(parseVehicleState('not json')).toBeNull();
  });

  it('keeps the trail bounded', () => {
    const trail = new TrailBuffer(3);
    for (let i = 0; i < 5; i += 1) {
      trail.add({ eastM: i, northM: i + 1, upM: i + 2, timestampMs: i });
    }
    expect(trail.values()).toHaveLength(3);
    expect(trail.values()[0].eastM).toBe(2);
  });

  it('builds trail points from local ENU state', () => {
    expect(trailPointFromState(message, 42)).toEqual({ eastM: 2, northM: 3, upM: 4, timestampMs: 42 });
  });

  it('reports stale and partial telemetry', () => {
    expect(isTelemetryStale(null)).toBe(true);
    expect(isTelemetryStale({ ...message, packetAgeS: 2.1 })).toBe(true);
    expect(isTelemetryStale({ ...message, packetAgeS: 0.5 })).toBe(false);
    expect(hasRenderablePosition(null)).toBe(false);
    expect(hasRenderablePosition({ ...message, localPosition: { eastM: null, northM: 3, upM: 4 } })).toBe(false);
    expect(hasRenderablePosition(message)).toBe(true);
  });

  it('converts yaw to display heading formats', () => {
    expect(headingDegFromYaw(0)).toBe(90);
    expect(headingDegFromYaw(Math.PI / 2)).toBe(0);
    expect(yawDegFromRad(Math.PI)).toBe(-180);
    expect(yawDegFromRad(-Math.PI / 2)).toBe(-90);
  });
});
