import { describe, expect, it } from 'vitest';
import { homePointFromVehicle, mapScreenToWorld, mapWorldToScreen, selectedMapVehicle, type MapViewState } from './map-panel';
import type { SessionSnapshotMessage, VehicleStateMessage } from './state';

const view: MapViewState = { scale: 2, panEastM: 5, panNorthM: -3, followSelected: true };

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
  globalPosition: { latDeg: 37, lonDeg: -122, altitudeM: 100, relativeAltitudeM: 10, originLatDeg: 37, originLonDeg: -122, originAltitudeM: 90 },
  localPosition: { eastM: 12, northM: 18, upM: 10 },
  velocity: { northMps: 0, eastMps: 0, downMps: 0 },
  metrics: { headingDeg: 0, airspeedMps: null, groundspeedMps: null, climbMps: null, throttlePct: null }
};

describe('map panel helpers', () => {
  it('round-trips local coordinates through screen transforms', () => {
    const center = { eastM: 10, northM: 20 };
    const world = { eastM: 22, northM: 35 };
    const screen = mapWorldToScreen(world, center, view, 1200, 760);
    expect(mapScreenToWorld(screen, center, view, 1200, 760)).toEqual(world);
  });

  it('selects the chosen vehicle with a first-vehicle fallback', () => {
    const snapshot: SessionSnapshotMessage = {
      type: 'session_snapshot',
      vehicles: [vehicle, { ...vehicle, id: '2:1', systemId: 2 }],
      selectedVehicleId: '2:1',
      messages: [],
      events: [],
      packetCount: 0,
      decodedCount: 0
    };
    expect(selectedMapVehicle(snapshot)?.id).toBe('2:1');
    expect(selectedMapVehicle({ ...snapshot, selectedVehicleId: 'missing' })?.id).toBe('1:1');
  });

  it('uses MAVLink origin as a home marker fallback', () => {
    expect(homePointFromVehicle(vehicle)).toEqual({ eastM: 0, northM: 0, upM: 0 });
    expect(homePointFromVehicle({ ...vehicle, home: { latDeg: 37.0001, lonDeg: -122, altitudeM: 95 } })?.northM).toBeGreaterThan(11);
  });
});
