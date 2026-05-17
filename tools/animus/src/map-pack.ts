export const ANIMUS_MAP_PACK_URL = './maps/altair-topo.pmtiles';
export const ANIMUS_MAP_PACK_LABEL = 'Altair bundled topographic map';
export const ANIMUS_MAP_PACK_PATH = 'dist/maps/altair-topo.pmtiles';

export type MapPackStatus = {
  available: boolean;
  url: string;
  label: string;
  error?: string;
};

export function normalizeMapPackStatus(value: unknown): MapPackStatus {
  const defaults = createUnavailableMapPackStatus('map pack status unavailable');
  if (!isRecord(value)) return defaults;
  const url = typeof value.url === 'string' && value.url.trim() ? value.url.trim() : ANIMUS_MAP_PACK_URL;
  const label = typeof value.label === 'string' && value.label.trim() ? value.label.trim().slice(0, 120) : ANIMUS_MAP_PACK_LABEL;
  const error = typeof value.error === 'string' && value.error.trim() ? value.error.trim().slice(0, 240) : undefined;
  return {
    available: value.available === true,
    url,
    label,
    ...(error ? { error } : {})
  };
}

export function createUnavailableMapPackStatus(error: string): MapPackStatus {
  return {
    available: false,
    url: ANIMUS_MAP_PACK_URL,
    label: ANIMUS_MAP_PACK_LABEL,
    error
  };
}

function isRecord(value: unknown): value is Record<string, unknown> {
  return typeof value === 'object' && value !== null;
}
