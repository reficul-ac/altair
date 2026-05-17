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

export function buildFlightTerrainModel(status: MapCacheStatus | null, vehicle: VehicleStateMessage | null, gridSize = 17, spacingM = 45): FlightTerrainModel {
  void status;
  void vehicle;
  return { available: false, reason: 'DEM cache support is not implemented in v1.', textured: false, samples: [], gridSize, spacingM };
}
