import { constants } from 'node:fs';
import { access, mkdir, readFile, rm, stat, writeFile } from 'node:fs/promises';
import path from 'node:path';
import {
  ANIMUS_MAP_CACHE_DEFAULT_MAX_TILE_COUNT,
  ANIMUS_MAP_CACHE_DEFAULT_MIN_ZOOM,
  ANIMUS_MAP_CACHE_DEFAULT_MAX_ZOOM,
  createEmptyMapCacheDownloadState,
  estimateTileCount,
  localTileUrlTemplate,
  normalizeMapCacheSet,
  tileUrl,
  tilesForZoom,
  validateTileUrlTemplate,
  type MapCacheActionResult,
  type MapCacheDownloadRequest,
  type MapCacheDownloadState,
  type MapCacheEstimate,
  type MapCacheEstimateRequest,
  type MapCacheSet,
  type MapCacheStatus
} from './map-cache.js';

type MapCacheIndex = {
  schemaVersion: 1;
  activeSetId: string | null;
  sets: MapCacheSet[];
};

const INDEX_FILENAME = 'index.json';
const TILE_EMPTY_PNG = Buffer.from(
  'iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAYAAAAfFcSJAAAADUlEQVR42mP8z8BQDwAFgwJ/lU6n1wAAAABJRU5ErkJggg==',
  'base64'
);

export class MapCacheManager {
  private downloadState: MapCacheDownloadState | null = null;
  private abortController: AbortController | null = null;

  constructor(private readonly userDataPath: string) {}

  cacheRoot(): string {
    return path.join(this.userDataPath, 'map-cache');
  }

  async status(): Promise<MapCacheStatus> {
    const index = await this.readIndex();
    const activeSet = index.sets.find((set) => set.id === index.activeSetId) ?? null;
    const available = activeSet ? await this.hasAnyTile(activeSet) : false;
    return {
      available,
      activeSet: available ? activeSet : null,
      sets: index.sets,
      downloadState: this.downloadState,
      error: available ? null : activeSet ? 'active offline satellite cache has no readable tiles' : 'offline satellite cache unavailable'
    };
  }

  async estimate(request: MapCacheEstimateRequest): Promise<MapCacheEstimate> {
    return estimateTileCount(request);
  }

  async startDownload(request: MapCacheDownloadRequest): Promise<MapCacheActionResult> {
    if (this.downloadState?.active) return { ok: false, status: await this.status(), error: 'offline satellite cache download is already active' };
    const validation = validateTileUrlTemplate(request.urlTemplate);
    if (!validation.ok) return { ok: false, status: await this.status(), error: validation.error };
    const estimate = estimateTileCount({
      bbox: request.bbox,
      minZoom: request.minZoom ?? ANIMUS_MAP_CACHE_DEFAULT_MIN_ZOOM,
      maxZoom: request.maxZoom ?? ANIMUS_MAP_CACHE_DEFAULT_MAX_ZOOM,
      maxTileCount: request.maxTileCount ?? ANIMUS_MAP_CACHE_DEFAULT_MAX_TILE_COUNT
    });
    if (estimate.exceedsLimit) {
      return { ok: false, status: await this.status(), error: `tile estimate ${estimate.tileCount} exceeds max ${estimate.maxTileCount}` };
    }

    const now = new Date().toISOString();
    const setId = `cache-${Date.now().toString(36)}`;
    const label = normalizeLabel(request.label) ?? `Offline satellite ${now.slice(0, 10)}`;
    const set: MapCacheSet = {
      id: setId,
      label,
      templateHost: validation.host,
      attribution: normalizeLabel(request.attribution),
      bbox: estimate.bbox,
      minZoom: estimate.minZoom,
      maxZoom: estimate.maxZoom,
      tileCount: estimate.tileCount,
      downloadedCount: 0,
      failedCount: 0,
      bytes: 0,
      createdAt: now,
      updatedAt: now,
      extension: validation.extension
    };

    this.abortController = new AbortController();
    this.downloadState = {
      ...createEmptyMapCacheDownloadState(),
      active: true,
      setId,
      label,
      queued: estimate.tileCount,
      minZoom: estimate.minZoom,
      maxZoom: estimate.maxZoom,
      bbox: estimate.bbox
    };
    void this.downloadTiles(request.urlTemplate, set, this.abortController).catch((error) => {
      if (this.downloadState) {
        this.downloadState.active = false;
        this.downloadState.lastError = error instanceof Error ? error.message : 'offline satellite cache download failed';
      }
    });
    return { ok: true, status: await this.status() };
  }

  async cancelDownload(): Promise<MapCacheActionResult> {
    this.abortController?.abort();
    if (this.downloadState) {
      this.downloadState.active = false;
      this.downloadState.cancelled = true;
      this.downloadState.lastError = 'download cancelled';
    }
    return { ok: true, status: await this.status() };
  }

  async activate(setId: string): Promise<MapCacheActionResult> {
    const index = await this.readIndex();
    if (!index.sets.some((set) => set.id === setId)) return { ok: false, status: await this.status(), error: 'cache set not found' };
    await this.writeIndex({ ...index, activeSetId: setId });
    return { ok: true, status: await this.status() };
  }

  async delete(setId: string): Promise<MapCacheActionResult> {
    const index = await this.readIndex();
    const next = {
      schemaVersion: 1 as const,
      activeSetId: index.activeSetId === setId ? null : index.activeSetId,
      sets: index.sets.filter((set) => set.id !== setId)
    };
    await rm(path.join(this.cacheRoot(), 'tiles', setId), { recursive: true, force: true });
    await this.writeIndex(next);
    return { ok: true, status: await this.status() };
  }

  async tilePath(setId: string, z: string, x: string, y: string): Promise<{ path: string; found: boolean }> {
    const index = await this.readIndex();
    const set = index.sets.find((candidate) => candidate.id === setId);
    const safeY = y.replace(/\.[a-z0-9]+$/i, '');
    const tileFile = set ? path.join(this.cacheRoot(), 'tiles', set.id, z, x, `${safeY}.${set.extension}`) : '';
    if (!set || !safeSegment(z) || !safeSegment(x) || !safeSegment(safeY)) {
      return { path: '', found: false };
    }
    try {
      await access(tileFile, constants.R_OK);
      return { path: tileFile, found: true };
    } catch {
      return { path: '', found: false };
    }
  }

  emptyTileBytes(): ArrayBuffer {
    return TILE_EMPTY_PNG.buffer.slice(TILE_EMPTY_PNG.byteOffset, TILE_EMPTY_PNG.byteOffset + TILE_EMPTY_PNG.byteLength);
  }

  localTileTemplateForActive(set: MapCacheSet): string {
    return localTileUrlTemplate(set.id);
  }

  private async downloadTiles(template: string, set: MapCacheSet, abortController: AbortController): Promise<void> {
    await mkdir(path.join(this.cacheRoot(), 'tiles', set.id), { recursive: true });
    let downloaded = 0;
    let failed = 0;
    let bytes = 0;
    for (let zoom = set.minZoom; zoom <= set.maxZoom; zoom += 1) {
      for (const tile of tilesForZoom(set.bbox, zoom)) {
        if (abortController.signal.aborted) break;
        try {
          const response = await fetch(tileUrl(template, tile), { signal: abortController.signal });
          if (!response.ok) throw new Error(`HTTP ${response.status} for z${tile.z}/${tile.x}/${tile.y}`);
          const body = Buffer.from(await response.arrayBuffer());
          const tilePath = path.join(this.cacheRoot(), 'tiles', set.id, String(tile.z), String(tile.x), `${tile.y}.${set.extension}`);
          await mkdir(path.dirname(tilePath), { recursive: true });
          await writeFile(tilePath, body);
          downloaded += 1;
          bytes += body.byteLength;
        } catch (error) {
          if (abortController.signal.aborted) break;
          failed += 1;
          if (this.downloadState) this.downloadState.lastError = error instanceof Error ? error.message : 'tile download failed';
        }
        if (this.downloadState) {
          this.downloadState.downloaded = downloaded;
          this.downloadState.failed = failed;
          this.downloadState.bytes = bytes;
          this.downloadState.queued = Math.max(0, set.tileCount - downloaded - failed);
        }
      }
    }
    const finished = new Date().toISOString();
    const completedSet: MapCacheSet = {
      ...set,
      downloadedCount: downloaded,
      failedCount: failed,
      bytes,
      updatedAt: finished
    };
    const index = await this.readIndex();
    const nextSets = [...index.sets.filter((candidate) => candidate.id !== set.id), completedSet];
    await this.writeIndex({ schemaVersion: 1, activeSetId: downloaded > 0 ? set.id : index.activeSetId, sets: nextSets });
    if (this.downloadState) {
      this.downloadState.active = false;
      this.downloadState.cancelled = abortController.signal.aborted;
      if (abortController.signal.aborted) this.downloadState.lastError = 'download cancelled';
    }
    this.abortController = null;
  }

  private async hasAnyTile(set: MapCacheSet): Promise<boolean> {
    const root = path.join(this.cacheRoot(), 'tiles', set.id);
    try {
      const info = await stat(root);
      return info.isDirectory() && set.downloadedCount > 0;
    } catch {
      return false;
    }
  }

  private async readIndex(): Promise<MapCacheIndex> {
    try {
      const parsed = JSON.parse(await readFile(path.join(this.cacheRoot(), INDEX_FILENAME), 'utf8')) as unknown;
      if (!isRecord(parsed)) return createEmptyIndex();
      const sets = Array.isArray(parsed.sets) ? parsed.sets.map(normalizeMapCacheSet).filter((set): set is MapCacheSet => Boolean(set)) : [];
      const activeSetId = typeof parsed.activeSetId === 'string' && sets.some((set) => set.id === parsed.activeSetId) ? parsed.activeSetId : null;
      return { schemaVersion: 1, activeSetId, sets };
    } catch {
      return createEmptyIndex();
    }
  }

  private async writeIndex(index: MapCacheIndex): Promise<void> {
    await mkdir(this.cacheRoot(), { recursive: true });
    await writeFile(path.join(this.cacheRoot(), INDEX_FILENAME), `${JSON.stringify(index, null, 2)}\n`, 'utf8');
  }
}

function createEmptyIndex(): MapCacheIndex {
  return { schemaVersion: 1, activeSetId: null, sets: [] };
}

function normalizeLabel(value: unknown): string | null {
  return typeof value === 'string' && value.trim() ? value.trim().slice(0, 240) : null;
}

function safeSegment(value: string): boolean {
  return /^[0-9]+$/.test(value);
}

function isRecord(value: unknown): value is Record<string, unknown> {
  return typeof value === 'object' && value !== null;
}
