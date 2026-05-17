export const ANIMUS_MAP_CACHE_PROTOCOL = 'animus-cache';
export const ANIMUS_MAP_CACHE_DEFAULT_TEMPLATE = 'https://basemap.nationalmap.gov/arcgis/rest/services/USGSImageryOnly/MapServer/tile/{z}/{y}/{x}';
export const ANIMUS_MAP_CACHE_DEFAULT_ATTRIBUTION = 'USGS The National Map';
export const ANIMUS_MAP_CACHE_DEFAULT_MIN_ZOOM = 12;
export const ANIMUS_MAP_CACHE_DEFAULT_MAX_ZOOM = 16;
export const ANIMUS_MAP_CACHE_DEFAULT_MAX_TILE_COUNT = 8000;
export const ANIMUS_MAP_CACHE_DEFAULT_RADIUS_M = 1600;
export const ANIMUS_MAP_CACHE_DEFAULT_ORIGIN = { latDeg: 37.4275, lonDeg: -122.1697 };
export const ANIMUS_DEM_CACHE_DEFAULT_TEMPLATE = 'https://s3.amazonaws.com/elevation-tiles-prod/terrarium/{z}/{x}/{y}.png';
export const ANIMUS_DEM_CACHE_DEFAULT_ATTRIBUTION = 'AWS Open Data Terrain Tiles';
export const ANIMUS_DEM_CACHE_DEFAULT_ENCODING: DemEncoding = 'terrarium';
export const ANIMUS_DEM_CACHE_DEFAULT_MAX_ZOOM = 15;

export type MapCacheBbox = {
  west: number;
  south: number;
  east: number;
  north: number;
};

export type MapCacheSet = {
  id: string;
  label: string;
  templateHost: string;
  attribution: string | null;
  bbox: MapCacheBbox;
  minZoom: number;
  maxZoom: number;
  tileCount: number;
  downloadedCount: number;
  failedCount: number;
  bytes: number;
  createdAt: string;
  updatedAt: string;
  extension: string;
  encoding?: DemEncoding;
};

export type DemEncoding = 'terrarium' | 'mapbox';

export type MapCacheDownloadState = {
  active: boolean;
  setId: string | null;
  label: string | null;
  queued: number;
  downloaded: number;
  failed: number;
  bytes: number;
  minZoom: number | null;
  maxZoom: number | null;
  bbox: MapCacheBbox | null;
  lastError: string | null;
  cancelled: boolean;
};

export type MapCacheStatus = {
  available: boolean;
  activeSet: MapCacheSet | null;
  sets: MapCacheSet[];
  downloadState: MapCacheDownloadState | null;
  error: string | null;
};

export type MapCacheEstimateRequest = {
  bbox: MapCacheBbox;
  minZoom: number;
  maxZoom: number;
  maxTileCount?: number | null;
};

export type MapCacheEstimate = {
  bbox: MapCacheBbox;
  minZoom: number;
  maxZoom: number;
  tileCount: number;
  exceedsLimit: boolean;
  maxTileCount: number;
};

export type MapCacheDownloadRequest = MapCacheEstimateRequest & {
  urlTemplate: string;
  attribution?: string | null;
  label?: string | null;
  encoding?: DemEncoding | null;
};

export type MapCacheActionResult = {
  ok: boolean;
  status: MapCacheStatus;
  error?: string;
};

export function createEmptyMapCacheDownloadState(): MapCacheDownloadState {
  return {
    active: false,
    setId: null,
    label: null,
    queued: 0,
    downloaded: 0,
    failed: 0,
    bytes: 0,
    minZoom: null,
    maxZoom: null,
    bbox: null,
    lastError: null,
    cancelled: false
  };
}

export function createUnavailableMapCacheStatus(error = 'offline satellite cache unavailable'): MapCacheStatus {
  return {
    available: false,
    activeSet: null,
    sets: [],
    downloadState: null,
    error
  };
}

export function normalizeMapCacheStatus(value: unknown): MapCacheStatus {
  if (!isRecord(value)) return createUnavailableMapCacheStatus();
  const sets = Array.isArray(value.sets) ? value.sets.map(normalizeMapCacheSet).filter((set): set is MapCacheSet => Boolean(set)) : [];
  const activeSet = normalizeMapCacheSet(value.activeSet);
  const downloadState = normalizeDownloadState(value.downloadState);
  const error = normalizeOptionalString(value.error, 240);
  return {
    available: value.available === true && Boolean(activeSet),
    activeSet,
    sets,
    downloadState,
    error
  };
}

export function normalizeMapCacheSet(value: unknown): MapCacheSet | null {
  if (!isRecord(value)) return null;
  const id = normalizeOptionalString(value.id, 96);
  const bbox = normalizeBbox(value.bbox);
  if (!id || !bbox) return null;
  const minZoom = normalizeZoom(value.minZoom, ANIMUS_MAP_CACHE_DEFAULT_MIN_ZOOM);
  const maxZoom = normalizeZoom(value.maxZoom, Math.max(minZoom, ANIMUS_MAP_CACHE_DEFAULT_MAX_ZOOM));
  return {
    id,
    label: normalizeOptionalString(value.label, 120) ?? 'Offline satellite cache',
    templateHost: normalizeOptionalString(value.templateHost, 240) ?? 'licensed XYZ source',
    attribution: normalizeOptionalString(value.attribution, 240),
    bbox,
    minZoom,
    maxZoom: Math.max(minZoom, maxZoom),
    tileCount: normalizeCount(value.tileCount),
    downloadedCount: normalizeCount(value.downloadedCount),
    failedCount: normalizeCount(value.failedCount),
    bytes: normalizeCount(value.bytes),
    createdAt: normalizeOptionalString(value.createdAt, 64) ?? new Date(0).toISOString(),
    updatedAt: normalizeOptionalString(value.updatedAt, 64) ?? new Date(0).toISOString(),
    extension: normalizeTileExtension(value.extension),
    encoding: normalizeDemEncoding(value.encoding)
  };
}

export function validateTileUrlTemplate(template: string): { ok: true; host: string; extension: string } | { ok: false; error: string } {
  const trimmed = template.trim();
  if (!trimmed) return { ok: false, error: 'tile URL template is required' };
  if (!trimmed.includes('{z}') || !trimmed.includes('{x}') || !trimmed.includes('{y}')) {
    return { ok: false, error: 'tile URL template must include {z}, {x}, and {y}' };
  }
  let parsed: URL;
  try {
    parsed = new URL(trimmed.replaceAll('{z}', '0').replaceAll('{x}', '0').replaceAll('{y}', '0'));
  } catch {
    return { ok: false, error: 'tile URL template must be a valid URL' };
  }
  if (parsed.protocol !== 'http:' && parsed.protocol !== 'https:') {
    return { ok: false, error: 'tile URL template must use http or https' };
  }
  const extension = extensionFromPath(parsed.pathname);
  return { ok: true, host: parsed.host, extension };
}

export function estimateTileCount(request: MapCacheEstimateRequest): MapCacheEstimate {
  const bbox = normalizeBbox(request.bbox) ?? bboxAround(ANIMUS_MAP_CACHE_DEFAULT_ORIGIN.latDeg, ANIMUS_MAP_CACHE_DEFAULT_ORIGIN.lonDeg, ANIMUS_MAP_CACHE_DEFAULT_RADIUS_M);
  const minZoom = normalizeZoom(request.minZoom, ANIMUS_MAP_CACHE_DEFAULT_MIN_ZOOM);
  const maxZoom = Math.max(minZoom, normalizeZoom(request.maxZoom, ANIMUS_MAP_CACHE_DEFAULT_MAX_ZOOM));
  const maxTileCount = normalizeCount(request.maxTileCount, ANIMUS_MAP_CACHE_DEFAULT_MAX_TILE_COUNT);
  let tileCount = 0;
  for (let zoom = minZoom; zoom <= maxZoom; zoom += 1) {
    tileCount += tilesForZoom(bbox, zoom).length;
  }
  return { bbox, minZoom, maxZoom, tileCount, exceedsLimit: tileCount > maxTileCount, maxTileCount };
}

export function tilesForZoom(bbox: MapCacheBbox, zoom: number): { z: number; x: number; y: number }[] {
  const z = normalizeZoom(zoom, ANIMUS_MAP_CACHE_DEFAULT_MIN_ZOOM);
  const west = Math.min(bbox.west, bbox.east);
  const east = Math.max(bbox.west, bbox.east);
  const south = Math.min(bbox.south, bbox.north);
  const north = Math.max(bbox.south, bbox.north);
  const nw = lonLatToTile(west, north, z);
  const se = lonLatToTile(east, south, z);
  const maxIndex = 2 ** z - 1;
  const minX = clampInt(Math.min(nw.x, se.x), 0, maxIndex);
  const maxX = clampInt(Math.max(nw.x, se.x), 0, maxIndex);
  const minY = clampInt(Math.min(nw.y, se.y), 0, maxIndex);
  const maxY = clampInt(Math.max(nw.y, se.y), 0, maxIndex);
  const tiles: { z: number; x: number; y: number }[] = [];
  for (let x = minX; x <= maxX; x += 1) {
    for (let y = minY; y <= maxY; y += 1) {
      tiles.push({ z, x, y });
    }
  }
  return tiles;
}

export function tileUrl(template: string, tile: { z: number; x: number; y: number }): string {
  return template.replaceAll('{z}', String(tile.z)).replaceAll('{x}', String(tile.x)).replaceAll('{y}', String(tile.y));
}

export function localTileUrlTemplate(setId: string): string {
  return `${ANIMUS_MAP_CACHE_PROTOCOL}://tiles/${encodeURIComponent(setId)}/{z}/{x}/{y}`;
}

export function localDemTileUrlTemplate(setId: string): string {
  return `${ANIMUS_MAP_CACHE_PROTOCOL}://dem/${encodeURIComponent(setId)}/{z}/{x}/{y}`;
}

export function decodeRgbDemPixel(red: number, green: number, blue: number, encoding: DemEncoding): number {
  const r = clampInt(red, 0, 255);
  const g = clampInt(green, 0, 255);
  const b = clampInt(blue, 0, 255);
  if (encoding === 'mapbox') return -10000 + ((r * 256 * 256 + g * 256 + b) * 0.1);
  return (r * 256 + g + b / 256) - 32768;
}

export function bboxAround(latDeg: number, lonDeg: number, radiusM: number): MapCacheBbox {
  const earthRadiusM = 6378137;
  const latDelta = (radiusM / earthRadiusM) * (180 / Math.PI);
  const lonDelta = (radiusM / (earthRadiusM * Math.cos((latDeg * Math.PI) / 180))) * (180 / Math.PI);
  return normalizeBbox({ west: lonDeg - lonDelta, south: latDeg - latDelta, east: lonDeg + lonDelta, north: latDeg + latDelta })!;
}

export function normalizeBbox(value: unknown): MapCacheBbox | null {
  if (!isRecord(value)) return null;
  const west = normalizeLon(value.west);
  const east = normalizeLon(value.east);
  const south = normalizeLat(value.south);
  const north = normalizeLat(value.north);
  if (west === null || east === null || south === null || north === null) return null;
  return {
    west: Math.min(west, east),
    south: Math.min(south, north),
    east: Math.max(west, east),
    north: Math.max(south, north)
  };
}

function normalizeDownloadState(value: unknown): MapCacheDownloadState | null {
  if (!isRecord(value)) return null;
  return {
    active: value.active === true,
    setId: normalizeOptionalString(value.setId, 96),
    label: normalizeOptionalString(value.label, 120),
    queued: normalizeCount(value.queued),
    downloaded: normalizeCount(value.downloaded),
    failed: normalizeCount(value.failed),
    bytes: normalizeCount(value.bytes),
    minZoom: typeof value.minZoom === 'number' ? normalizeZoom(value.minZoom, ANIMUS_MAP_CACHE_DEFAULT_MIN_ZOOM) : null,
    maxZoom: typeof value.maxZoom === 'number' ? normalizeZoom(value.maxZoom, ANIMUS_MAP_CACHE_DEFAULT_MAX_ZOOM) : null,
    bbox: normalizeBbox(value.bbox),
    lastError: normalizeOptionalString(value.lastError, 240),
    cancelled: value.cancelled === true
  };
}

function lonLatToTile(lonDeg: number, latDeg: number, z: number): { x: number; y: number } {
  const latRad = (normalizeLat(latDeg) ?? 0) * Math.PI / 180;
  const n = 2 ** z;
  return {
    x: Math.floor(((normalizeLon(lonDeg) ?? 0) + 180) / 360 * n),
    y: Math.floor((1 - Math.log(Math.tan(latRad) + 1 / Math.cos(latRad)) / Math.PI) / 2 * n)
  };
}

function normalizeZoom(value: unknown, fallback: number): number {
  const number = typeof value === 'number' ? value : Number(value);
  if (!Number.isFinite(number)) return fallback;
  return clampInt(Math.round(number), 0, 22);
}

function normalizeLat(value: unknown): number | null {
  const number = typeof value === 'number' ? value : Number(value);
  if (!Number.isFinite(number)) return null;
  return Math.max(-85.05112878, Math.min(85.05112878, number));
}

function normalizeLon(value: unknown): number | null {
  const number = typeof value === 'number' ? value : Number(value);
  if (!Number.isFinite(number)) return null;
  return Math.max(-180, Math.min(180, number));
}

function normalizeCount(value: unknown, fallback = 0): number {
  const number = typeof value === 'number' ? value : Number(value);
  if (!Number.isFinite(number) || number < 0) return fallback;
  return Math.floor(number);
}

function normalizeTileExtension(value: unknown): string {
  const normalized = normalizeOptionalString(value, 12)?.replace(/[^a-z0-9]/gi, '').toLowerCase();
  return normalized || 'png';
}

export function normalizeDemEncoding(value: unknown): DemEncoding | undefined {
  return value === 'mapbox' || value === 'terrarium' ? value : undefined;
}

function extensionFromPath(pathname: string): string {
  const match = /\.([a-z0-9]+)$/i.exec(pathname);
  const extension = match?.[1]?.toLowerCase();
  if (extension === 'jpg' || extension === 'jpeg') return extension;
  if (extension === 'webp') return 'webp';
  return 'png';
}

function clampInt(value: number, min: number, max: number): number {
  return Math.max(min, Math.min(max, Math.floor(value)));
}

function normalizeOptionalString(value: unknown, maxLength: number): string | null {
  return typeof value === 'string' && value.trim() ? value.trim().slice(0, maxLength) : null;
}

function isRecord(value: unknown): value is Record<string, unknown> {
  return typeof value === 'object' && value !== null;
}
