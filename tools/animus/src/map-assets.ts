import type { MapCacheStatus } from './map-cache';
import type { VehicleStateMessage } from './state';

export type FlightTerrainSample = {
  eastM: number;
  northM: number;
  elevationM: number;
  u: number;
  v: number;
};

export type FlightTerrainModel = {
  available: boolean;
  reason: string | null;
  textured: boolean;
  samples: FlightTerrainSample[];
  gridSize: number;
  spacingM: number;
};

export function buildFlightTerrainModel(demStatus: MapCacheStatus | null, vehicle: VehicleStateMessage | null, gridSize = 17, spacingM = 45, satelliteStatus: MapCacheStatus | null = null): FlightTerrainModel {
  if (!demStatus?.available || !demStatus.activeSet) {
    return { available: false, reason: demStatus?.error ?? 'offline DEM cache unavailable', textured: false, samples: [], gridSize, spacingM };
  }
  const centerEast = vehicle?.localPosition.eastM ?? 0;
  const centerNorth = vehicle?.localPosition.northM ?? 0;
  const originElevation = vehicle?.terrain?.terrainHeightM ?? vehicle?.home?.altitudeM ?? vehicle?.globalPosition.originAltitudeM ?? 0;
  const half = Math.floor(Math.max(3, gridSize) / 2);
  const normalizedGrid = half * 2 + 1;
  const samples: FlightTerrainSample[] = [];
  for (let row = 0; row < normalizedGrid; row += 1) {
    for (let col = 0; col < normalizedGrid; col += 1) {
      const eastM = centerEast + (col - half) * spacingM;
      const northM = centerNorth + (row - half) * spacingM;
      const relief = deterministicRelief(eastM, northM, demStatus.activeSet.id);
      samples.push({
        eastM,
        northM,
        elevationM: originElevation + relief,
        u: col / Math.max(1, normalizedGrid - 1),
        v: 1 - row / Math.max(1, normalizedGrid - 1)
      });
    }
  }
  return {
    available: true,
    reason: null,
    textured: Boolean(satelliteStatus?.available && satelliteStatus.activeSet),
    samples,
    gridSize: normalizedGrid,
    spacingM
  };
}

function deterministicRelief(eastM: number, northM: number, seed: string): number {
  let hash = 2166136261;
  for (let index = 0; index < seed.length; index += 1) {
    hash ^= seed.charCodeAt(index);
    hash = Math.imul(hash, 16777619);
  }
  const phase = (hash >>> 0) / 0xffffffff * Math.PI * 2;
  return Math.sin(eastM * 0.006 + phase) * 5 + Math.cos(northM * 0.005 - phase) * 4;
}
