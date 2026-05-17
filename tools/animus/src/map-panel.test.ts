import { describe, expect, it } from 'vitest';
import { buildMapOverlayModel, geofenceLocalPoints, homePointFromVehicle, mapMode, mapScreenToWorld, mapWorldToScreen, rallyLocalPoints, satelliteStyle, selectedMapVehicle, setMapMode, terrainModelFromVehicle, terrainStyle, type MapViewState } from './map-panel';
import { buildFlightTerrainModel, localTerrainTileUrl, lonLatToXyzPixel, sampleRgbDemImageData } from './map-assets';
import { normalizeMapCacheStatus } from './map-cache';
import type { SessionSnapshotMessage, VehicleStateMessage } from './state';

const view: MapViewState = { scale: 2, panEastM: 5, panNorthM: -3, followSelected: true, mode: 'satellite' };

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

  it('converts geofence and rally overlays to local map coordinates', () => {
    const enriched: VehicleStateMessage = {
      ...vehicle,
      geofences: [
        { id: 'keep-in', kind: 'circle', inclusion: true, center: { latDeg: 37.0001, lonDeg: -122 }, radiusM: 50 },
        { id: 'keep-out', kind: 'polygon', inclusion: false, vertices: [{ latDeg: 37, lonDeg: -122 }, { latDeg: 37.0001, lonDeg: -122 }, { latDeg: 37, lonDeg: -121.9999 }] }
      ],
      rallyPoints: [{ id: 'r1', latDeg: 37.0001, lonDeg: -122, altitudeM: 120 }]
    };
    expect(geofenceLocalPoints(enriched)).toHaveLength(2);
    expect(geofenceLocalPoints(enriched)[0].radiusM).toBe(50);
    expect(rallyLocalPoints(enriched)[0].northM).toBeGreaterThan(11);
  });

  it('builds local terrain samples from terrain reports', () => {
    const model = terrainModelFromVehicle({
      ...vehicle,
      terrain: { latDeg: 37.0001, lonDeg: -122, spacingM: 30, terrainHeightM: 92, currentHeightM: 18, pending: 0, loaded: 8 }
    });

    expect(model?.samples).toHaveLength(25);
    expect(model?.spacingM).toBe(30);
    expect(model?.maxTerrainM).toBe(model?.minTerrainM);
  });

  it('allows terrain 3D mode when selected by the UI', () => {
    setMapMode('terrain-3d');
    expect(mapMode()).toBe('terrain-3d');
    setMapMode('satellite');
    expect(mapMode()).toBe('satellite');
  });

  it('normalizes map-cache status from the preload boundary', () => {
    expect(normalizeMapCacheStatus({
      available: true,
      activeSet: { id: 'set-1', label: 'Field', templateHost: 'tiles.example', attribution: 'Licensed', bbox: { west: -122, south: 37, east: -121, north: 38 }, minZoom: 12, maxZoom: 14, tileCount: 3, downloadedCount: 3, failedCount: 0, bytes: 10, createdAt: '2026-01-01T00:00:00.000Z', updatedAt: '2026-01-01T00:00:00.000Z', extension: 'png' },
      sets: [],
      error: null
    })).toEqual({
      available: true,
      activeSet: { id: 'set-1', label: 'Field', templateHost: 'tiles.example', attribution: 'Licensed', bbox: { west: -122, south: 37, east: -121, north: 38 }, minZoom: 12, maxZoom: 14, tileCount: 3, downloadedCount: 3, failedCount: 0, bytes: 10, createdAt: '2026-01-01T00:00:00.000Z', updatedAt: '2026-01-01T00:00:00.000Z', extension: 'png', encoding: undefined },
      sets: [],
      downloadState: null,
      error: null
    });
    expect(normalizeMapCacheStatus({ available: false, error: 'missing' }).available).toBe(false);
  });

  it('builds a MapLibre satellite style with local cache tiles', () => {
    const style = satelliteStyle(normalizeMapCacheStatus({
      available: true,
      activeSet: { id: 'set-1', label: 'Field', templateHost: 'tiles.example', attribution: 'Licensed', bbox: { west: -122, south: 37, east: -121, north: 38 }, minZoom: 12, maxZoom: 14, tileCount: 3, downloadedCount: 3, failedCount: 0, bytes: 10, createdAt: '2026-01-01T00:00:00.000Z', updatedAt: '2026-01-01T00:00:00.000Z', extension: 'png' },
      sets: []
    }));
    expect(style.sources.satellite.type).toBe('raster');
    expect('terrain' in style.sources).toBe(false);
    expect(JSON.stringify(style.sources.satellite)).toContain('animus-cache://tiles/set-1');
    expect(style.layers.some((layer) => layer.id === 'satellite')).toBe(true);
    expect(style.terrain).toBeUndefined();
  });

  it('builds a MapLibre terrain style with raster-dem tiles', () => {
    const satellite = normalizeMapCacheStatus({
      available: true,
      activeSet: { id: 'set-1', label: 'Field', templateHost: 'tiles.example', attribution: 'Licensed', bbox: { west: -122, south: 37, east: -121, north: 38 }, minZoom: 12, maxZoom: 14, tileCount: 3, downloadedCount: 3, failedCount: 0, bytes: 10, createdAt: '2026-01-01T00:00:00.000Z', updatedAt: '2026-01-01T00:00:00.000Z', extension: 'png' },
      sets: []
    });
    const dem = normalizeMapCacheStatus({
      available: true,
      activeSet: { id: 'dem-1', label: 'Field DEM', templateHost: 'dem.example', attribution: 'Licensed DEM', bbox: { west: -122, south: 37, east: -121, north: 38 }, minZoom: 12, maxZoom: 14, tileCount: 3, downloadedCount: 3, failedCount: 0, bytes: 10, createdAt: '2026-01-01T00:00:00.000Z', updatedAt: '2026-01-01T00:00:00.000Z', extension: 'png', encoding: 'mapbox' },
      sets: []
    });
    const style = terrainStyle(satellite, dem);
    expect(style.sources.dem.type).toBe('raster-dem');
    expect(JSON.stringify(style.sources.dem)).toContain('animus-cache://dem/dem-1');
    expect(style.terrain).toEqual({ source: 'dem', exaggeration: 1 });
  });

  it('builds flight terrain from sampled RGB DEM cache elevations', () => {
    const unavailable = buildFlightTerrainModel(normalizeMapCacheStatus({ available: false, error: 'missing' }), vehicle);
    expect(unavailable.available).toBe(false);
    const dem = normalizeMapCacheStatus({
      available: true,
      activeSet: { id: 'dem-1', label: 'Field DEM', templateHost: 'dem.example', attribution: 'Licensed DEM', bbox: { west: -122, south: 37, east: -121, north: 38 }, minZoom: 12, maxZoom: 14, tileCount: 3, downloadedCount: 3, failedCount: 0, bytes: 10, createdAt: '2026-01-01T00:00:00.000Z', updatedAt: '2026-01-01T00:00:00.000Z', extension: 'png', encoding: 'terrarium' },
      sets: []
    });
    const satellite = normalizeMapCacheStatus({
      available: true,
      activeSet: { id: 'set-1', label: 'Field', templateHost: 'tiles.example', attribution: 'Licensed', bbox: { west: -122, south: 37, east: -121, north: 38 }, minZoom: 12, maxZoom: 14, tileCount: 3, downloadedCount: 3, failedCount: 0, bytes: 10, createdAt: '2026-01-01T00:00:00.000Z', updatedAt: '2026-01-01T00:00:00.000Z', extension: 'png' },
      sets: []
    });
    const available = buildFlightTerrainModel(dem, vehicle, {
      gridSize: 5,
      spacingM: 30,
      satelliteStatus: satellite,
      demSampler: ({ eastM, northM }) => 120 + eastM * 0.1 + northM * 0.01
    });
    expect(available.available).toBe(true);
    expect(available.samples).toHaveLength(25);
    expect(available.sourceStatus).toBe('sampled');
    expect(available.hasSatelliteTexture).toBe(true);
    expect(available.samples[12].elevationM).toBeCloseTo(121.38, 6);
  });

  it('keeps missing DEM tiles unavailable or partial without throwing', () => {
    const dem = normalizeMapCacheStatus({
      available: true,
      activeSet: { id: 'dem-1', label: 'Field DEM', templateHost: 'dem.example', attribution: 'Licensed DEM', bbox: { west: -122, south: 37, east: -121, north: 38 }, minZoom: 12, maxZoom: 14, tileCount: 3, downloadedCount: 3, failedCount: 0, bytes: 10, createdAt: '2026-01-01T00:00:00.000Z', updatedAt: '2026-01-01T00:00:00.000Z', extension: 'png', encoding: 'terrarium' },
      sets: []
    });
    expect(buildFlightTerrainModel(dem, vehicle, { gridSize: 3, demSampler: () => null })).toMatchObject({ available: false, sourceStatus: 'unavailable' });
    const partial = buildFlightTerrainModel(dem, vehicle, { gridSize: 3, demSampler: ({ eastM }) => eastM > 12 ? 95 : null });
    expect(partial.available).toBe(true);
    expect(partial.sourceStatus).toBe('partial');
    expect(partial.missingDemSamples).toBeGreaterThan(0);
  });

  it('converts lon/lat to XYZ tile pixel coordinates and local cache URLs', () => {
    expect(lonLatToXyzPixel(0, 0, 1)).toMatchObject({ z: 1, x: 1, y: 1, pixelX: 0, pixelY: 0 });
    const coordinate = lonLatToXyzPixel(-122.1697, 37.4275, 14);
    expect(coordinate.x).toBeGreaterThan(2600);
    expect(coordinate.y).toBeGreaterThan(6300);
    expect(localTerrainTileUrl('dem', 'dem set', { z: 14, x: 1, y: 2 })).toBe('animus-cache://dem/dem%20set/14/1/2');
  });

  it('bilinearly samples Terrarium and Mapbox DEM pixels from image data', () => {
    const terrarium = new Uint8ClampedArray([
      128, 0, 0, 255,
      128, 1, 0, 255,
      128, 2, 0, 255,
      128, 3, 0, 255
    ]);
    expect(sampleRgbDemImageData(terrarium, 2, 2, 0.5, 0.5, 'terrarium')).toBeCloseTo(1.5, 6);
    const mapbox = new Uint8ClampedArray([
      1, 134, 160, 255,
      1, 134, 170, 255,
      1, 134, 180, 255,
      1, 134, 190, 255
    ]);
    expect(sampleRgbDemImageData(mapbox, 2, 2, 0, 0, 'mapbox')).toBeCloseTo(0, 5);
    expect(sampleRgbDemImageData(mapbox, 2, 2, 1, 1, 'mapbox')).toBeCloseTo(3, 5);
  });

  it('converts session overlays to geographic GeoJSON', () => {
    const snapshot: SessionSnapshotMessage = {
      type: 'session_snapshot',
      vehicles: [{
        ...vehicle,
        mission: { activeSeq: 1, waypoints: [{ seq: 0, latDeg: 37, lonDeg: -122, altitudeM: 100 }, { seq: 1, latDeg: 37.0001, lonDeg: -122, altitudeM: 110 }] },
        home: { latDeg: 37, lonDeg: -122, altitudeM: 90 },
        rallyPoints: [{ id: 'r1', latDeg: 37.0002, lonDeg: -122, altitudeM: 120 }]
      }],
      selectedVehicleId: '1:1',
      messages: [],
      events: [{ id: 'e1', timestampS: 1, vehicleId: '1:1', level: 'warning', kind: 'marker', label: 'Gate', position: { eastM: 0, northM: 10, upM: 0 } }],
      packetCount: 0,
      decodedCount: 0
    };
    const model = buildMapOverlayModel(snapshot);
    expect(model.center?.lat).toBe(37);
    expect(model.vehicles.features).toHaveLength(1);
    expect(model.trails.features).toHaveLength(0);
    expect(model.mission.features.length).toBeGreaterThan(1);
    expect(model.home.features).toHaveLength(1);
    expect(model.rally.features).toHaveLength(1);
    expect(model.events.features).toHaveLength(1);
  });

  it('falls back to local overlay geometry when global coordinates are missing', () => {
    const localOnly = {
      ...vehicle,
      globalPosition: { latDeg: null, lonDeg: null, altitudeM: null, relativeAltitudeM: null, originLatDeg: null, originLonDeg: null, originAltitudeM: null },
      home: undefined,
      trail: [{ eastM: 12, northM: 18, upM: 10, timestampS: 1 }, { eastM: 22, northM: 28, upM: 10, timestampS: 2 }]
    };
    const model = buildMapOverlayModel({
      type: 'session_snapshot',
      vehicles: [localOnly],
      selectedVehicleId: '1:1',
      messages: [],
      events: [],
      packetCount: 0,
      decodedCount: 0
    });
    expect(model.center?.lat).toBeGreaterThan(37);
    expect(model.trails.features).toHaveLength(1);
  });
});
