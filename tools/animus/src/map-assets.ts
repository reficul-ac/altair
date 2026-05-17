import { decodeRgbDemPixel, localDemTileUrlTemplate, localTileUrlTemplate, type DemEncoding, type MapCacheStatus } from './map-cache';
import type { VehicleStateMessage } from './state';

const EARTH_RADIUS_M = 6378137;
const MAX_MERCATOR_LAT = 85.05112878;

export type XyzPixelCoordinate = {
  z: number;
  x: number;
  y: number;
  pixelX: number;
  pixelY: number;
  worldPixelX: number;
  worldPixelY: number;
};

export type FlightTerrainSample = {
  eastM: number;
  northM: number;
  latDeg: number;
  lonDeg: number;
  elevationM: number;
  demAvailable: boolean;
  u: number;
  v: number;
};

export type FlightTerrainSourceStatus = 'unavailable' | 'sampled' | 'partial';

export type FlightTerrainModel = {
  available: boolean;
  reason: string | null;
  sourceStatus: FlightTerrainSourceStatus;
  hasSatelliteTexture: boolean;
  samples: FlightTerrainSample[];
  gridSize: number;
  spacingM: number;
  demZoom: number | null;
  satelliteZoom: number | null;
  missingDemSamples: number;
  missingSatelliteTiles: number;
};

export type FlightTerrainDemSampler = (point: {
  latDeg: number;
  lonDeg: number;
  eastM: number;
  northM: number;
  z: number;
}) => number | null | undefined;

export type FlightTerrainOptions = {
  gridSize?: number;
  spacingM?: number;
  satelliteStatus?: MapCacheStatus | null;
  demSampler?: FlightTerrainDemSampler | null;
  missingSatelliteTiles?: number;
};

export function lonLatToXyzPixel(lonDeg: number, latDeg: number, z: number, tileSize = 256): XyzPixelCoordinate {
  const zoom = clampInt(Math.round(z), 0, 22);
  const scale = 2 ** zoom;
  const lon = clamp(lonDeg, -180, 180);
  const lat = clamp(latDeg, -MAX_MERCATOR_LAT, MAX_MERCATOR_LAT);
  const xWorld = ((lon + 180) / 360) * scale * tileSize;
  const sinLat = Math.sin((lat * Math.PI) / 180);
  const yWorld = (0.5 - Math.log((1 + sinLat) / (1 - sinLat)) / (4 * Math.PI)) * scale * tileSize;
  const maxWorld = scale * tileSize - 1;
  const worldPixelX = clamp(xWorld, 0, maxWorld);
  const worldPixelY = clamp(yWorld, 0, maxWorld);
  const x = clampInt(Math.floor(worldPixelX / tileSize), 0, scale - 1);
  const y = clampInt(Math.floor(worldPixelY / tileSize), 0, scale - 1);
  return {
    z: zoom,
    x,
    y,
    pixelX: worldPixelX - x * tileSize,
    pixelY: worldPixelY - y * tileSize,
    worldPixelX,
    worldPixelY
  };
}

export function localTerrainTileUrl(kind: 'dem' | 'tiles', setId: string, tile: { z: number; x: number; y: number }): string {
  const template = kind === 'dem' ? localDemTileUrlTemplate(setId) : localTileUrlTemplate(setId);
  return template.replaceAll('{z}', String(tile.z)).replaceAll('{x}', String(tile.x)).replaceAll('{y}', String(tile.y));
}

export function sampleRgbDemImageData(data: Uint8ClampedArray, width: number, height: number, pixelX: number, pixelY: number, encoding: DemEncoding): number | null {
  if (width <= 0 || height <= 0 || data.length < width * height * 4) return null;
  const x0 = clampInt(Math.floor(pixelX), 0, width - 1);
  const y0 = clampInt(Math.floor(pixelY), 0, height - 1);
  const x1 = clampInt(x0 + 1, 0, width - 1);
  const y1 = clampInt(y0 + 1, 0, height - 1);
  const tx = clamp(pixelX - x0, 0, 1);
  const ty = clamp(pixelY - y0, 0, 1);
  const h00 = demAt(data, width, x0, y0, encoding);
  const h10 = demAt(data, width, x1, y0, encoding);
  const h01 = demAt(data, width, x0, y1, encoding);
  const h11 = demAt(data, width, x1, y1, encoding);
  const top = h00 * (1 - tx) + h10 * tx;
  const bottom = h01 * (1 - tx) + h11 * tx;
  return top * (1 - ty) + bottom * ty;
}

export function localToLonLat(eastM: number, northM: number, originLatDeg: number, originLonDeg: number): { latDeg: number; lonDeg: number } {
  const originLatRad = (originLatDeg * Math.PI) / 180;
  const latDeg = originLatDeg + (northM / EARTH_RADIUS_M) * (180 / Math.PI);
  const lonDeg = originLonDeg + (eastM / (EARTH_RADIUS_M * Math.max(0.01, Math.cos(originLatRad)))) * (180 / Math.PI);
  return { latDeg, lonDeg };
}

export function buildFlightTerrainModel(demStatus: MapCacheStatus | null, vehicle: VehicleStateMessage | null, gridSizeOrOptions: number | FlightTerrainOptions = 17, spacingM = 45, satelliteStatus: MapCacheStatus | null = null): FlightTerrainModel {
  const options: FlightTerrainOptions = typeof gridSizeOrOptions === 'number'
    ? { gridSize: gridSizeOrOptions, spacingM, satelliteStatus }
    : gridSizeOrOptions;
  const gridSize = options.gridSize ?? 17;
  const sampleSpacingM = options.spacingM ?? 45;
  const demSet = demStatus?.available ? demStatus.activeSet : null;
  if (!demSet) {
    return unavailableTerrain(demStatus?.error ?? 'offline DEM cache unavailable', gridSize, sampleSpacingM);
  }
  const originLat = vehicle?.globalPosition.originLatDeg ?? vehicle?.home?.latDeg ?? vehicle?.globalPosition.latDeg;
  const originLon = vehicle?.globalPosition.originLonDeg ?? vehicle?.home?.lonDeg ?? vehicle?.globalPosition.lonDeg;
  if (originLat === undefined || originLat === null || originLon === undefined || originLon === null) {
    return unavailableTerrain('vehicle global origin unavailable', gridSize, sampleSpacingM);
  }
  const demZoom = demSet.maxZoom;
  const satelliteSet = options.satelliteStatus?.available ? options.satelliteStatus.activeSet : null;
  const satelliteZoom = satelliteSet ? satelliteSet.maxZoom : null;
  const centerEast = vehicle?.localPosition.eastM ?? 0;
  const centerNorth = vehicle?.localPosition.northM ?? 0;
  const fallbackElevation = vehicle?.terrain?.terrainHeightM ?? vehicle?.home?.altitudeM ?? vehicle?.globalPosition.originAltitudeM ?? 0;
  const half = Math.floor(Math.max(3, gridSize) / 2);
  const normalizedGrid = half * 2 + 1;
  const samples: FlightTerrainSample[] = [];
  let sampled = 0;
  let missingDemSamples = 0;
  for (let row = 0; row < normalizedGrid; row += 1) {
    for (let col = 0; col < normalizedGrid; col += 1) {
      const eastM = centerEast + (col - half) * sampleSpacingM;
      const northM = centerNorth + (row - half) * sampleSpacingM;
      const geo = localToLonLat(eastM, northM, originLat, originLon);
      const sampledElevation = options.demSampler?.({ ...geo, eastM, northM, z: demZoom });
      const demAvailable = Number.isFinite(sampledElevation);
      if (demAvailable) sampled += 1;
      else missingDemSamples += 1;
      samples.push({
        eastM,
        northM,
        latDeg: geo.latDeg,
        lonDeg: geo.lonDeg,
        elevationM: demAvailable ? sampledElevation as number : fallbackElevation,
        demAvailable,
        u: col / Math.max(1, normalizedGrid - 1),
        v: 1 - row / Math.max(1, normalizedGrid - 1)
      });
    }
  }
  if (sampled === 0) {
    return {
      available: false,
      reason: 'DEM tiles unavailable for the current flight area',
      sourceStatus: 'unavailable',
      hasSatelliteTexture: false,
      samples: [],
      gridSize: normalizedGrid,
      spacingM: sampleSpacingM,
      demZoom,
      satelliteZoom,
      missingDemSamples,
      missingSatelliteTiles: options.missingSatelliteTiles ?? 0
    };
  }
  return {
    available: true,
    reason: missingDemSamples > 0 ? 'DEM coverage is incomplete for the current flight area' : null,
    sourceStatus: missingDemSamples > 0 ? 'partial' : 'sampled',
    hasSatelliteTexture: Boolean(satelliteSet && (options.missingSatelliteTiles ?? 0) === 0),
    samples,
    gridSize: normalizedGrid,
    spacingM: sampleSpacingM,
    demZoom,
    satelliteZoom,
    missingDemSamples,
    missingSatelliteTiles: options.missingSatelliteTiles ?? 0
  };
}

function unavailableTerrain(reason: string, gridSize: number, spacingM: number): FlightTerrainModel {
  return {
    available: false,
    reason,
    sourceStatus: 'unavailable',
    hasSatelliteTexture: false,
    samples: [],
    gridSize: Math.floor(Math.max(3, gridSize) / 2) * 2 + 1,
    spacingM,
    demZoom: null,
    satelliteZoom: null,
    missingDemSamples: 0,
    missingSatelliteTiles: 0
  };
}

function demAt(data: Uint8ClampedArray, width: number, x: number, y: number, encoding: DemEncoding): number {
  const offset = (y * width + x) * 4;
  return decodeRgbDemPixel(data[offset] ?? 0, data[offset + 1] ?? 0, data[offset + 2] ?? 0, encoding);
}

function clamp(value: number, min: number, max: number): number {
  return Math.min(max, Math.max(min, value));
}

function clampInt(value: number, min: number, max: number): number {
  return Math.min(max, Math.max(min, Math.trunc(value)));
}
