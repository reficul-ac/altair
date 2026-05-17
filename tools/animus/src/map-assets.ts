import type { MapPackStatus } from './map-pack';
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

export function buildFlightTerrainModel(status: MapPackStatus | null, vehicle: VehicleStateMessage | null, gridSize = 17, spacingM = 45): FlightTerrainModel {
  if (!status?.terrain.available) {
    return { available: false, reason: status?.terrain.error ?? 'Terrain DEM PMTiles is unavailable.', textured: false, samples: [], gridSize, spacingM };
  }
  const centerEastM = vehicle?.localPosition.eastM ?? 0;
  const centerNorthM = vehicle?.localPosition.northM ?? 0;
  const baseElevationM = vehicle?.globalPosition.originAltitudeM ?? vehicle?.home?.altitudeM ?? 0;
  const samples: FlightTerrainSample[] = [];
  const half = Math.floor(gridSize / 2);
  for (let row = 0; row < gridSize; row += 1) {
    for (let col = 0; col < gridSize; col += 1) {
      const eastM = centerEastM + (col - half) * spacingM;
      const northM = centerNorthM + (row - half) * spacingM;
      samples.push({
        eastM,
        northM,
        elevationM: baseElevationM,
        u: col / Math.max(1, gridSize - 1),
        v: row / Math.max(1, gridSize - 1)
      });
    }
  }
  return {
    available: true,
    reason: null,
    textured: status.satellite.available,
    samples,
    gridSize,
    spacingM
  };
}
