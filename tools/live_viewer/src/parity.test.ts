import { describe, expect, it } from 'vitest';
import { buildMultiVehicleAnalysis, createMockLink, defaultCommandCapabilities, evaluateGuardedCommand } from './parity';
import type { SessionSnapshotMessage, VehicleStateMessage } from './state';

const vehicle: VehicleStateMessage = {
  type: 'vehicle_state',
  id: '1:1',
  connected: true,
  packetAgeS: 0,
  heartbeatAgeS: 0,
  systemId: 1,
  componentId: 1,
  vehicleType: 'Fixed-wing',
  attitude: { rollRad: 0, pitchRad: 0, yawRad: 0, rollRateRps: 0, pitchRateRps: 0, yawRateRps: 0 },
  globalPosition: { latDeg: null, lonDeg: null, altitudeM: null, relativeAltitudeM: null, originLatDeg: null, originLonDeg: null, originAltitudeM: null },
  localPosition: { eastM: 0, northM: 0, upM: 0 },
  velocity: { northMps: 0, eastMps: 0, downMps: 0 },
  metrics: { headingDeg: null, airspeedMps: null, groundspeedMps: null, climbMps: null, throttlePct: null }
};

function snapshot(vehicles: VehicleStateMessage[]): SessionSnapshotMessage {
  return {
    type: 'session_snapshot',
    vehicles,
    selectedVehicleId: vehicles[0]?.id ?? null,
    messages: [],
    events: [],
    packetCount: 0,
    decodedCount: 0
  };
}

describe('ground station parity helpers', () => {
  it('blocks commands unless live writable capability and confirmation are present', () => {
    expect(evaluateGuardedCommand({ command: 'arm', vehicleId: '1:1', confirmed: false }, defaultCommandCapabilities(true, true)).accepted).toBe(false);
    expect(evaluateGuardedCommand({ command: 'arm', vehicleId: '1:1', confirmed: true }, defaultCommandCapabilities(false, false)).reason).toContain('no live');
    expect(evaluateGuardedCommand({ command: 'arm', vehicleId: '1:1', confirmed: true }, defaultCommandCapabilities(true, false)).reason).toContain('writable');
    expect(evaluateGuardedCommand({ command: 'arm', vehicleId: '1:1', confirmed: true }, defaultCommandCapabilities(true, true)).accepted).toBe(true);
  });

  it('computes formation offsets and deconfliction warnings', () => {
    const analysis = buildMultiVehicleAnalysis(snapshot([
      vehicle,
      { ...vehicle, id: '2:1', systemId: 2, localPosition: { eastM: 10, northM: 0, upM: 0 } },
      { ...vehicle, id: '3:1', systemId: 3, localPosition: { eastM: 100, northM: 0, upM: 0 } }
    ]), 5);
    expect(analysis.targetVehicleCount).toBe(12);
    expect(analysis.formation.centroid?.eastM).toBeCloseTo(110 / 3);
    expect(analysis.deconfliction.conflicts).toHaveLength(1);
    expect(analysis.deconfliction.conflicts[0].timestampS).toBe(5);
  });

  it('creates bounded mock links for offline workflows', () => {
    expect(createMockLink(99).vehicleCount).toBe(32);
    expect(createMockLink(0).vehicleCount).toBe(1);
  });
});
