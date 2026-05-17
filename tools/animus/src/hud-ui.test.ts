import { describe, expect, it } from 'vitest';
import { displayDirectionFromVehicle, groundTrackDegreesFromVelocity, normalizeCompassDegrees } from './hud-ui';
import type { VehicleStateMessage } from './state';

const baseVehicle: VehicleStateMessage = {
  type: 'vehicle_state',
  connected: true,
  packetAgeS: 0,
  heartbeatAgeS: 0,
  systemId: 1,
  componentId: 1,
  vehicleType: 'Fixed-wing',
  attitude: { rollRad: 0, pitchRad: 0, yawRad: 0, rollRateRps: 0, pitchRateRps: 0, yawRateRps: 0 },
  globalPosition: { latDeg: null, lonDeg: null, altitudeM: null, relativeAltitudeM: null, originLatDeg: null, originLonDeg: null, originAltitudeM: null },
  localPosition: { northM: null, eastM: null, upM: null },
  velocity: { northMps: 0, eastMps: 0, downMps: 0 },
  metrics: { headingDeg: null, airspeedMps: null, groundspeedMps: null, climbMps: null, throttlePct: null }
};

function vehicle(overrides: Partial<VehicleStateMessage>): VehicleStateMessage {
  return {
    ...baseVehicle,
    ...overrides,
    attitude: { ...baseVehicle.attitude, ...overrides.attitude },
    velocity: { ...baseVehicle.velocity, ...overrides.velocity },
    metrics: { ...baseVehicle.metrics, ...overrides.metrics }
  };
}

describe('HUD compass helpers', () => {
  it('normalizes compass degrees into one revolution', () => {
    expect(normalizeCompassDegrees(360)).toBe(0);
    expect(normalizeCompassDegrees(725)).toBe(5);
    expect(normalizeCompassDegrees(-10)).toBe(350);
  });

  it('computes cardinal ground-track directions from north/east velocity', () => {
    expect(groundTrackDegreesFromVelocity({ northMps: 2, eastMps: 0 })).toBeCloseTo(0, 6);
    expect(groundTrackDegreesFromVelocity({ northMps: 0, eastMps: 2 })).toBeCloseTo(90, 6);
    expect(groundTrackDegreesFromVelocity({ northMps: -2, eastMps: 0 })).toBeCloseTo(180, 6);
    expect(groundTrackDegreesFromVelocity({ northMps: 0, eastMps: -2 })).toBeCloseTo(270, 6);
  });

  it('selects ground track before heading and yaw fallback', () => {
    expect(displayDirectionFromVehicle(vehicle({
      velocity: { northMps: 0, eastMps: 3, downMps: 0 },
      metrics: { headingDeg: 12, airspeedMps: null, groundspeedMps: null, climbMps: null, throttlePct: null },
      attitude: { yawRad: Math.PI, rollRad: 0, pitchRad: 0, rollRateRps: 0, pitchRateRps: 0, yawRateRps: 0 }
    }))).toMatchObject({ degrees: 90, source: 'track', label: 'TRK' });
  });

  it('falls back to MAVLink heading when ground speed is near zero', () => {
    expect(displayDirectionFromVehicle(vehicle({
      velocity: { northMps: 0.02, eastMps: 0.02, downMps: 0 },
      metrics: { headingDeg: 362, airspeedMps: null, groundspeedMps: null, climbMps: null, throttlePct: null }
    }))).toMatchObject({ degrees: 2, source: 'heading', label: 'HDG' });
  });

  it('falls back to yaw-derived heading when velocity and heading are unavailable', () => {
    expect(displayDirectionFromVehicle(vehicle({
      attitude: { yawRad: Math.PI / 2, rollRad: 0, pitchRad: 0, rollRateRps: 0, pitchRateRps: 0, yawRateRps: 0 }
    }))).toMatchObject({ degrees: 0, source: 'yaw', label: 'YAW' });
  });
});
