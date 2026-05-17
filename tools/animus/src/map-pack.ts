export const ANIMUS_MAP_PACK_URL = './maps/altair-topo.pmtiles';
export const ANIMUS_MAP_PACK_LABEL = 'Altair bundled development map';
export const ANIMUS_MAP_PACK_PATH = 'dist/maps/altair-topo.pmtiles';

export type MapPackAssetStatus = {
  available: boolean;
  url: string | null;
  path: string | null;
  label: string;
  attribution: string | null;
  error?: string;
};

export type MapPackStatus = {
  available: boolean;
  satellite: MapPackAssetStatus;
  terrain: MapPackAssetStatus;
  label: string;
  attribution: string | null;
  error?: string;
};

export function normalizeMapPackStatus(value: unknown): MapPackStatus {
  const defaults = createUnavailableMapPackStatus('map pack status unavailable');
  if (!isRecord(value)) return defaults;
  const satellite = normalizeAssetStatus(value.satellite, 'Satellite imagery');
  const terrain = normalizeAssetStatus(value.terrain, 'Terrain DEM');
  const label = normalizeOptionalString(value.label, 120) ?? labelFromAssets(satellite, terrain);
  const attribution = normalizeOptionalString(value.attribution, 240) ?? satellite.attribution ?? terrain.attribution;
  const error = typeof value.error === 'string' && value.error.trim() ? value.error.trim().slice(0, 240) : undefined;
  return {
    available: value.available === true && satellite.available && terrain.available,
    satellite,
    terrain,
    label,
    attribution,
    ...(error ? { error } : {})
  };
}

export function createUnavailableMapPackStatus(error: string): MapPackStatus {
  const satellite = createUnavailableAssetStatus('Satellite imagery', error);
  const terrain = createUnavailableAssetStatus('Terrain DEM', error);
  return {
    available: false,
    satellite,
    terrain,
    label: 'Offline satellite map unavailable',
    attribution: null,
    error
  };
}

export function createUnavailableAssetStatus(label: string, error: string): MapPackAssetStatus {
  return {
    available: false,
    url: null,
    path: null,
    label,
    attribution: null,
    error
  };
}

export function createDevelopmentMapPackStatus(): MapPackStatus {
  const asset: MapPackAssetStatus = {
    available: true,
    url: ANIMUS_MAP_PACK_URL,
    path: ANIMUS_MAP_PACK_PATH,
    label: ANIMUS_MAP_PACK_LABEL,
    attribution: 'Generated Altair offline development basemap'
  };
  return {
    available: true,
    satellite: { ...asset, label: 'Development satellite placeholder' },
    terrain: { ...asset, label: 'Development terrain placeholder' },
    label: ANIMUS_MAP_PACK_LABEL,
    attribution: asset.attribution
  };
}

function normalizeAssetStatus(value: unknown, fallbackLabel: string): MapPackAssetStatus {
  if (!isRecord(value)) return createUnavailableAssetStatus(fallbackLabel, `${fallbackLabel} PMTiles path is not configured.`);
  const label = normalizeOptionalString(value.label, 120) ?? fallbackLabel;
  const error = normalizeOptionalString(value.error, 240) ?? undefined;
  return {
    available: value.available === true,
    url: normalizeOptionalString(value.url, 4096),
    path: normalizeOptionalString(value.path, 4096),
    label,
    attribution: normalizeOptionalString(value.attribution, 240),
    ...(error ? { error } : {})
  };
}

function labelFromAssets(satellite: MapPackAssetStatus, terrain: MapPackAssetStatus): string {
  if (satellite.available && terrain.available) return 'Offline satellite + terrain map pack';
  if (satellite.available) return 'Offline satellite imagery ready';
  if (terrain.available) return 'Offline terrain DEM ready';
  return 'Offline satellite map unavailable';
}

function normalizeOptionalString(value: unknown, maxLength: number): string | null {
  return typeof value === 'string' && value.trim() ? value.trim().slice(0, maxLength) : null;
}

function isRecord(value: unknown): value is Record<string, unknown> {
  return typeof value === 'object' && value !== null;
}
