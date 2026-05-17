import { access } from 'node:fs/promises';
import path from 'node:path';
import { ANIMUS_MAP_PACK_LABEL, ANIMUS_MAP_PACK_PATH, ANIMUS_MAP_PACK_URL, createUnavailableMapPackStatus, type MapPackStatus } from './map-pack.js';

export async function getBundledMapPackStatus(appRoot: string): Promise<MapPackStatus> {
  const filePath = path.join(appRoot, ANIMUS_MAP_PACK_PATH);
  try {
    await access(filePath);
    return { available: true, url: ANIMUS_MAP_PACK_URL, label: ANIMUS_MAP_PACK_LABEL };
  } catch (error) {
    return createUnavailableMapPackStatus(error instanceof Error ? error.message : 'map pack is missing or unreadable');
  }
}
