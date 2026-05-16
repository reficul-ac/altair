import { describe, expect, it } from 'vitest';
import { buildMultiVehicleAnalysis, buildVehicleReadiness, createMockLink, defaultCommandCapabilities, evaluateGuardedCommand, firmwareIdentity, normalizeFailsafeState, normalizeFlightMode, normalizeMissionState } from './parity';
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

function readyStatus(overrides: Partial<NonNullable<VehicleStateMessage['status']>> = {}): NonNullable<VehicleStateMessage['status']> {
  return {
    armed: false,
    mode: 'Auto',
    firmware: firmwareIdentity(12),
    baseMode: 0,
    customMode: 0,
    systemStatus: 4,
    gpsFix: '3D fix',
    satellitesVisible: 10,
    batteryRemainingPct: 80,
    batteryVoltageV: 12,
    onboardControlSensorsHealth: 1,
    missionSeq: 0,
    missionState: normalizeMissionState({ activeSeq: 0, totalItems: 2 }),
    lastStatusText: null,
    ...overrides
  };
}

describe('ground station parity helpers', () => {
  it('blocks commands unless live writable capability and confirmation are present', () => {
    expect(evaluateGuardedCommand({ command: 'arm', vehicleId: '1:1', confirmed: false }, defaultCommandCapabilities(true, true)).accepted).toBe(false);
    expect(evaluateGuardedCommand({ command: 'arm', vehicleId: '1:1', confirmed: true }, defaultCommandCapabilities(false, false)).reason).toContain('no live');
    expect(evaluateGuardedCommand({ command: 'arm', vehicleId: '1:1', confirmed: true }, defaultCommandCapabilities(true, false)).reason).toContain('writable');
    expect(evaluateGuardedCommand({ command: 'arm', vehicleId: '1:1', confirmed: true }, defaultCommandCapabilities(true, true)).accepted).toBe(true);
  });

  it('normalizes firmware modes and preserves unknown values as explicit states', () => {
    expect(firmwareIdentity(12)).toMatchObject({ family: 'px4', label: 'PX4' });
    expect(firmwareIdentity(3)).toMatchObject({ family: 'ardupilot', label: 'ArduPilot' });
    expect(normalizeFlightMode(12, (4 << 16) | (4 << 24), 0x80).label).toBe('Auto mission');
    expect(normalizeFlightMode(3, 10, 0x80).label).toBe('Auto');
    expect(normalizeFlightMode(99, 123, 0x80)).toMatchObject({
      family: 'unsupported',
      known: false,
      unsupportedReason: 'custom mode 123 needs a firmware-specific mapping'
    });
  });

  it('uses explicit stale and readiness block reasons for command capabilities', () => {
    const readiness = buildVehicleReadiness({ connected: true, packetAgeS: 3, status: vehicle.status });
    const capability = defaultCommandCapabilities(true, true, { packetAgeS: 3, readiness });
    expect(capability.stale).toBe(true);
    expect(capability.supported).toEqual([]);
    expect(capability.blockedReason).toContain('stale');
    expect(evaluateGuardedCommand({ command: 'arm', vehicleId: '1:1', confirmed: true }, capability).reason).toContain('stale');
  });

  it('reports estimator readiness from SYS_STATUS sensor health', () => {
    expect(buildVehicleReadiness({ connected: true, packetAgeS: 0, status: readyStatus({ onboardControlSensorsHealth: null }) }).checks.find((check) => check.key === 'estimator')).toMatchObject({ state: 'unknown' });
    expect(buildVehicleReadiness({ connected: true, packetAgeS: 0, status: readyStatus({ onboardControlSensorsHealth: 0 }) }).checks.find((check) => check.key === 'estimator')).toMatchObject({ state: 'blocked' });
    expect(buildVehicleReadiness({ connected: true, packetAgeS: 0, status: readyStatus({ onboardControlSensorsHealth: 5 }) }).checks.find((check) => check.key === 'estimator')).toMatchObject({ state: 'ready' });
  });

  it('warns and blocks on battery and power thresholds', () => {
    expect(buildVehicleReadiness({ connected: true, packetAgeS: 0, status: readyStatus({ batteryRemainingPct: null, batteryVoltageV: null }) }).checks.find((check) => check.key === 'battery')).toMatchObject({ state: 'unknown' });
    expect(buildVehicleReadiness({ connected: true, packetAgeS: 0, status: readyStatus({ batteryRemainingPct: 15 }) }).checks.find((check) => check.key === 'battery')).toMatchObject({ state: 'warning' });
    expect(buildVehicleReadiness({ connected: true, packetAgeS: 0, status: readyStatus({ batteryRemainingPct: 9 }) }).checks.find((check) => check.key === 'battery')).toMatchObject({ state: 'blocked' });
    expect(buildVehicleReadiness({ connected: true, packetAgeS: 0, status: readyStatus({ batteryVoltageV: 0 }) }).checks.find((check) => check.key === 'battery')).toMatchObject({ state: 'blocked' });
  });

  it('keeps safety response commands available when preflight readiness is blocked', () => {
    const readiness = buildVehicleReadiness({ connected: true, packetAgeS: 0, status: readyStatus({ onboardControlSensorsHealth: 0 }) });
    const capability = defaultCommandCapabilities(true, true, { packetAgeS: 0, readiness });
    expect(capability.supported).not.toContain('arm');
    expect(capability.supported).toEqual(expect.arrayContaining(['disarm', 'emergency-stop', 'pause', 'land', 'return-to-launch']));
    expect(capability.blockedCommands?.arm).toContain('Estimator blocks commands');
    expect(evaluateGuardedCommand({ command: 'arm', vehicleId: '1:1', confirmed: true }, capability)).toMatchObject({ accepted: false, reason: expect.stringContaining('Estimator blocks commands') });
    expect(evaluateGuardedCommand({ command: 'emergency-stop', vehicleId: '1:1', confirmed: true }, capability)).toMatchObject({ accepted: true });
  });

  it('normalizes MAVLink system status into typed failsafe states', () => {
    expect(normalizeFailsafeState(12, 4)).toMatchObject({ family: 'px4', status: 'active', commandBlocking: false });
    expect(normalizeFailsafeState(3, 3)).toMatchObject({ family: 'ardupilot', status: 'standby', commandBlocking: false });
    expect(normalizeFailsafeState(0, 5)).toMatchObject({ family: 'generic', status: 'critical', commandBlocking: true });
    expect(normalizeFailsafeState(12, 6)).toMatchObject({ status: 'emergency', commandBlocking: true });
    expect(normalizeFailsafeState(12, 7)).toMatchObject({ status: 'poweroff', commandBlocking: true });
    expect(normalizeFailsafeState(99, 99)).toMatchObject({ family: 'unsupported', status: 'unknown', known: false });
  });

  it('normalizes mission state and progress from decoded mission telemetry', () => {
    expect(normalizeMissionState({ activeSeq: null, totalItems: null })).toMatchObject({ state: 'unknown', valid: false });
    expect(normalizeMissionState({ activeSeq: null, totalItems: 4 })).toMatchObject({ state: 'not-started', progressPct: 0, valid: true });
    expect(normalizeMissionState({ activeSeq: 1, totalItems: 4, modeState: normalizeFlightMode(3, 10, 0x80) })).toMatchObject({ state: 'active', progressPct: 50, valid: true });
    expect(normalizeMissionState({ activeSeq: 1, totalItems: 4, modeState: normalizeFlightMode(3, 0, 0x80) })).toMatchObject({ state: 'paused', progressPct: 50, valid: true });
    expect(normalizeMissionState({ activeSeq: 4, totalItems: 4 })).toMatchObject({ state: 'complete', progressPct: 100, valid: true });
    expect(normalizeMissionState({ activeSeq: 0, totalItems: 2, lastAckType: 2 })).toMatchObject({ state: 'unknown', valid: false });
  });

  it('blocks readiness for critical failsafe and invalid mission state', () => {
    const readiness = buildVehicleReadiness({
      connected: true,
      packetAgeS: 0,
      status: {
        armed: false,
        mode: 'Auto',
        firmware: firmwareIdentity(12),
        baseMode: 0,
        customMode: 0,
        systemStatus: 5,
        gpsFix: '3D fix',
        satellitesVisible: 10,
        batteryRemainingPct: 80,
        batteryVoltageV: 12,
        onboardControlSensorsHealth: 1,
        missionSeq: 0,
        missionState: normalizeMissionState({ activeSeq: 0, totalItems: 2, lastAckType: 3 }),
        lastStatusText: null
      }
    });
    expect(readiness.overall).toBe('blocked');
    expect(readiness.checks.find((check) => check.key === 'failsafe')).toMatchObject({ state: 'blocked' });
    expect(readiness.checks.find((check) => check.key === 'mission')).toMatchObject({ state: 'blocked' });
    expect(defaultCommandCapabilities(true, true, { packetAgeS: 0, readiness }).blockedReason).toContain('blocks commands');
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
