import { describe, expect, it } from 'vitest';
import { buildDashboardWidgetView, dashboardCommandDisabledReason, renderDashboardWidgetRows } from './dashboard-widgets';
import type { SessionSnapshotMessage, VehicleStateMessage } from './state';

const vehicle: VehicleStateMessage = {
  type: 'vehicle_state',
  id: '1:1',
  connected: true,
  packetAgeS: 0.2,
  heartbeatAgeS: 0.4,
  systemId: 1,
  componentId: 1,
  vehicleType: 'Fixed-wing',
  attitude: { rollRad: 0.1, pitchRad: -0.2, yawRad: 0.3, rollRateRps: 0, pitchRateRps: 0, yawRateRps: 0 },
  globalPosition: { latDeg: 37.1, lonDeg: -122.1, altitudeM: 120, relativeAltitudeM: 30, originLatDeg: 37, originLonDeg: -122, originAltitudeM: 90 },
  localPosition: { northM: 10, eastM: 20, upM: 30 },
  velocity: { northMps: 1, eastMps: 2, downMps: -0.5 },
  metrics: { headingDeg: 42, airspeedMps: 18, groundspeedMps: 20, climbMps: 0.5, throttlePct: 45 },
  status: {
    armed: false,
    mode: 'AUTO',
    modeState: { label: 'Auto', family: 'px4', category: 'auto', baseMode: 1, customMode: 4, armed: false, known: true, unsupportedReason: null },
    firmware: { family: 'px4', autopilot: 12, source: 'heartbeat', label: 'PX4', unsupportedReason: null },
    baseMode: 1,
    customMode: 4,
    gpsFix: '3D fix',
    satellitesVisible: 12,
    batteryRemainingPct: 82,
    batteryVoltageV: 12.4,
    onboardControlSensorsHealth: null,
    missionSeq: 3,
    lastStatusText: 'ready'
  },
  commandCapabilities: {
    liveLink: true,
    writableLink: true,
    authority: 'sitl-writable',
    stale: false,
    supported: ['arm', 'disarm', 'pause', 'return-to-launch', 'land', 'takeoff', 'change-altitude'],
    blockedReason: null
  }
};

const snapshot: SessionSnapshotMessage = {
  type: 'session_snapshot',
  vehicles: [vehicle],
  selectedVehicleId: '1:1',
  messages: [],
  events: [{ id: 'evt-1', timestampS: 1, vehicleId: '1:1', level: 'info', kind: 'status', label: 'nominal', position: null }],
  packetCount: 10,
  decodedCount: 8
};

describe('dashboard widget catalog rendering', () => {
  it('renders status widgets from a session snapshot', () => {
    const view = buildDashboardWidgetView({ id: 'link', kind: 'link-freshness', span: 'compact' }, snapshot);
    const html = renderDashboardWidgetRows(view);

    expect(view.rows.map((row) => row.label)).toContain('Heartbeat age');
    expect(html).toContain('0.40 s');
  });

  it('makes empty vehicle state explicit', () => {
    const view = buildDashboardWidgetView({ id: 'identity', kind: 'identity-mode', span: 'compact' }, { ...snapshot, vehicles: [], selectedVehicleId: null });

    expect(view.empty).toBe('No selected vehicle stream');
    expect(renderDashboardWidgetRows(view)).toContain('No selected vehicle stream');
  });

  it('disables command widgets when read-only or blocked', () => {
    expect(dashboardCommandDisabledReason(vehicle, 'arm')).toBeNull();
    expect(dashboardCommandDisabledReason({ ...vehicle, commandCapabilities: { ...vehicle.commandCapabilities!, writableLink: false, blockedReason: 'read-only authority' } }, 'arm')).toBe('read-only authority');
    expect(dashboardCommandDisabledReason({ ...vehicle, commandCapabilities: { ...vehicle.commandCapabilities!, supported: ['arm'], blockedCommands: { land: 'land unsupported' } } }, 'land')).toBe('land unsupported');
  });
});
