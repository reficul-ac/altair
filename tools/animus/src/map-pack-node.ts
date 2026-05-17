import { access } from 'node:fs/promises';
import path from 'node:path';
import { pathToFileURL } from 'node:url';
import type { AnimusUiSettings } from './animus-settings.js';
import { ANIMUS_MAP_PACK_PATH, createDevelopmentMapPackStatus, createUnavailableAssetStatus, type MapPackAssetStatus, type MapPackStatus } from './map-pack.js';

export async function getBundledMapPackStatus(appRoot: string): Promise<MapPackStatus> {
  const filePath = path.join(appRoot, ANIMUS_MAP_PACK_PATH);
  try {
    await access(filePath);
    return createDevelopmentMapPackStatus();
  } catch (error) {
    const message = error instanceof Error ? error.message : 'map pack is missing or unreadable';
    return {
      available: false,
      satellite: createUnavailableAssetStatus('Development satellite placeholder', message),
      terrain: createUnavailableAssetStatus('Development terrain placeholder', message),
      label: 'Offline satellite map unavailable',
      attribution: null,
      error: message
    };
  }
}

export async function getUserMapPackStatus(settings: AnimusUiSettings): Promise<MapPackStatus> {
  const satellite = await statusForPath(settings.satellitePmtilesPath, settings.mapPackLabel ?? 'Satellite imagery', settings.mapPackAttribution);
  const terrain = await statusForPath(settings.terrainPmtilesPath, 'Terrain DEM', settings.mapPackAttribution);
  const missing = [satellite, terrain].filter((asset) => !asset.available).map((asset) => asset.error ?? `${asset.label} unavailable`);
  return {
    available: satellite.available && terrain.available,
    satellite,
    terrain,
    label: settings.mapPackLabel ?? 'Offline satellite + terrain map pack',
    attribution: settings.mapPackAttribution,
    ...(missing.length ? { error: missing.join(' / ') } : {})
  };
}

async function statusForPath(filePath: string | null, label: string, attribution: string | null): Promise<MapPackAssetStatus> {
  if (!filePath) return createUnavailableAssetStatus(label, `${label} PMTiles path is not configured.`);
  try {
    await access(filePath);
    return {
      available: true,
      url: pathToFileURL(filePath).href,
      path: filePath,
      label,
      attribution
    };
  } catch (error) {
    return createUnavailableAssetStatus(label, error instanceof Error ? error.message : `${label} PMTiles file is missing or unreadable.`);
  }
}
