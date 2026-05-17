import { mkdir, mkdtemp, readFile, writeFile } from 'node:fs/promises';
import os from 'node:os';
import path from 'node:path';
import { describe, expect, it } from 'vitest';
import { animusSettingsPath, createDefaultAnimusSettings, normalizeAnimusSettings, readAnimusSettings, writeAnimusSettings } from './animus-settings';
import { createDefaultDashboardLayout } from './dashboard-types';
import { dashboardLayoutPath, exportDashboardProfile, importDashboardProfile, readDashboardLayout, resetDashboardLayout, writeDashboardLayout } from './dashboard-settings';
import { decodeRgbDemPixel, estimateTileCount, validateTileUrlTemplate } from './map-cache';
import { MapCacheManager } from './map-cache-node';

describe('dashboard settings helpers', () => {
  it('reads missing files as the default layout without writing', async () => {
    const dir = await mkdtemp(path.join(os.tmpdir(), 'animus-dashboard-'));
    const filePath = dashboardLayoutPath(dir);

    expect(await readDashboardLayout(filePath)).toEqual(createDefaultDashboardLayout());
  });

  it('persists normalized layout json', async () => {
    const dir = await mkdtemp(path.join(os.tmpdir(), 'animus-dashboard-'));
    const filePath = dashboardLayoutPath(dir);
    const saved = await writeDashboardLayout(filePath, {
      schemaVersion: 1,
      widgets: [
        { id: 'dup', kind: 'link-freshness', span: 'compact' },
        { id: 'dup', kind: 'gps-battery', span: 'wide' }
      ]
    });

    expect(saved.widgets.map((widget) => widget.id)).toEqual(['dup', 'dup-2']);
    expect(JSON.parse(await readFile(filePath, 'utf8')).widgets).toHaveLength(2);
  });

  it('falls back for malformed saved files and reset writes defaults', async () => {
    const dir = await mkdtemp(path.join(os.tmpdir(), 'animus-dashboard-'));
    const filePath = dashboardLayoutPath(dir);
    await writeFile(filePath, 'not-json', 'utf8');

    expect(await readDashboardLayout(filePath)).toEqual(createDefaultDashboardLayout());
    expect(await resetDashboardLayout(filePath)).toEqual(createDefaultDashboardLayout());
    expect(JSON.parse(await readFile(filePath, 'utf8')).widgets.length).toBeGreaterThan(1);
  });

  it('exports normalized profile json with a trailing newline', async () => {
    const dir = await mkdtemp(path.join(os.tmpdir(), 'animus-dashboard-profile-'));
    const filePath = path.join(dir, 'profile.json');

    const exported = await exportDashboardProfile(filePath, {
      schemaVersion: 1,
      widgets: [
        { id: 'link', kind: 'link-freshness', span: 'invalid' },
        { id: 'identity', kind: 'identity-mode', span: 'wide' }
      ]
    } as unknown as Parameters<typeof exportDashboardProfile>[1]);
    const raw = await readFile(filePath, 'utf8');

    expect(exported.widgets.map((widget) => widget.span)).toEqual(['compact', 'wide']);
    expect(raw.endsWith('\n')).toBe(true);
    expect(JSON.parse(raw)).toEqual(exported);
  });

  it('imports a valid dashboard profile', async () => {
    const dir = await mkdtemp(path.join(os.tmpdir(), 'animus-dashboard-profile-'));
    const filePath = path.join(dir, 'profile.json');
    await writeFile(filePath, JSON.stringify({
      schemaVersion: 1,
      widgets: [
        { id: 'mission', kind: 'mission-progress', span: 'full' }
      ]
    }), 'utf8');

    expect(await importDashboardProfile(filePath)).toEqual({
      schemaVersion: 1,
      widgets: [
        { id: 'mission', kind: 'mission-progress', span: 'full' }
      ]
    });
  });

  it('normalizes imported duplicate ids, invalid spans, and unknown widget kinds', async () => {
    const dir = await mkdtemp(path.join(os.tmpdir(), 'animus-dashboard-profile-'));
    const filePath = path.join(dir, 'profile.json');
    await writeFile(filePath, JSON.stringify({
      schemaVersion: 1,
      widgets: [
        { id: 'dup', kind: 'link-freshness', span: 'wide' },
        { id: 'dup', kind: 'gps-battery', span: 'tall' },
        { id: 'unknown', kind: 'unknown-kind', span: 'full' }
      ]
    }), 'utf8');

    expect(await importDashboardProfile(filePath)).toEqual({
      schemaVersion: 1,
      widgets: [
        { id: 'dup', kind: 'link-freshness', span: 'wide' },
        { id: 'dup-2', kind: 'gps-battery', span: 'compact' }
      ]
    });
  });

  it('imports malformed profile json as the default layout without throwing', async () => {
    const dir = await mkdtemp(path.join(os.tmpdir(), 'animus-dashboard-profile-'));
    const filePath = path.join(dir, 'profile.json');
    await writeFile(filePath, 'not-json', 'utf8');

    expect(await importDashboardProfile(filePath)).toEqual(createDefaultDashboardLayout());
  });

  it('reads missing application settings as safe defaults', async () => {
    const dir = await mkdtemp(path.join(os.tmpdir(), 'animus-settings-'));
    const filePath = animusSettingsPath(dir);

    expect(await readAnimusSettings(filePath)).toEqual(createDefaultAnimusSettings());
  });

  it('persists normalized UI-safe application settings only', async () => {
    const dir = await mkdtemp(path.join(os.tmpdir(), 'animus-settings-'));
    const filePath = animusSettingsPath(dir);
    const saved = await writeAnimusSettings(filePath, {
      schemaVersion: 3,
      defaultWorkspace: 'dashboard',
      theme: 'snow',
      cameraMode: 'fpv',
      cameraLock: true,
      mapStyle: 'satellite',
      mapFollowSelected: false,
      mapTileUrlTemplate: 'https://tiles.example/{z}/{x}/{y}.png?key=test',
      mapTileAttribution: 'Licensed test data',
      activeMapCacheSetId: 'cache-test',
      mapCacheMinZoom: 11,
      mapCacheMaxZoom: 15,
      mapCacheMaxTileCount: 1234,
      demTileUrlTemplate: 'https://dem.example/{z}/{x}/{y}.png?key=test',
      demTileAttribution: 'Licensed test DEM',
      demTileEncoding: 'mapbox',
      activeDemCacheSetId: 'dem-test',
      demCacheMinZoom: 10,
      demCacheMaxZoom: 14,
      demCacheMaxTileCount: 4321,
      lastDashboardPresetLabel: 'Flight Test'
    });

    expect(saved).toEqual({
      schemaVersion: 3,
      defaultWorkspace: 'dashboard',
      theme: 'snow',
      cameraMode: 'fpv',
      cameraLock: true,
      mapStyle: 'satellite',
      mapFollowSelected: false,
      mapTileUrlTemplate: 'https://tiles.example/{z}/{x}/{y}.png?key=test',
      mapTileAttribution: 'Licensed test data',
      activeMapCacheSetId: 'cache-test',
      mapCacheMinZoom: 11,
      mapCacheMaxZoom: 15,
      mapCacheMaxTileCount: 1234,
      demTileUrlTemplate: 'https://dem.example/{z}/{x}/{y}.png?key=test',
      demTileAttribution: 'Licensed test DEM',
      demTileEncoding: 'mapbox',
      activeDemCacheSetId: 'dem-test',
      demCacheMinZoom: 10,
      demCacheMaxZoom: 14,
      demCacheMaxTileCount: 4321,
      lastDashboardPresetLabel: 'Flight Test'
    });
    expect(JSON.parse(await readFile(filePath, 'utf8')).writableAnimus).toBeUndefined();
  });

  it('migrates v1 settings to offline satellite defaults', () => {
    expect(normalizeAnimusSettings({
      schemaVersion: 1,
      defaultWorkspace: 'map',
      theme: 'grid',
      cameraMode: 'chase',
      cameraLock: false,
      lastDashboardPresetLabel: null
    })).toEqual({ ...createDefaultAnimusSettings(), defaultWorkspace: 'map' });
  });

  it('migrates v2 PMTiles settings to v3 cache defaults without preserving map-pack paths', () => {
    expect(normalizeAnimusSettings({
      schemaVersion: 2,
      defaultWorkspace: 'map',
      theme: 'grid',
      cameraMode: 'chase',
      cameraLock: false,
      mapFollowSelected: true,
      satellitePmtilesPath: '/maps/sat.pmtiles',
      terrainPmtilesPath: '/maps/dem.pmtiles',
      mapPackLabel: 'Old pack',
      mapPackAttribution: 'Old attribution'
    })).toEqual({ ...createDefaultAnimusSettings(), defaultWorkspace: 'map' });
  });

  it('falls back for malformed application settings', async () => {
    const dir = await mkdtemp(path.join(os.tmpdir(), 'animus-settings-'));
    const filePath = animusSettingsPath(dir);
    await writeFile(filePath, 'not-json', 'utf8');

    expect(await readAnimusSettings(filePath)).toEqual(createDefaultAnimusSettings());
  });

  it('estimates XYZ tile counts deterministically and enforces guardrails', () => {
    const estimate = estimateTileCount({
      bbox: { west: -122.18, south: 37.42, east: -122.16, north: 37.44 },
      minZoom: 12,
      maxZoom: 13,
      maxTileCount: 1
    });
    expect(estimate.tileCount).toBeGreaterThan(0);
    expect(estimate).toEqual(estimateTileCount({ ...estimate, maxTileCount: 1 }));
    expect(estimate.exceedsLimit).toBe(true);
  });

  it('validates licensed XYZ URL templates before download', () => {
    expect(validateTileUrlTemplate('https://tiles.example/{z}/{x}/{y}.jpg?key=test')).toEqual({ ok: true, host: 'tiles.example', extension: 'jpg' });
    expect(validateTileUrlTemplate('file:///tiles/{z}/{x}/{y}.png').ok).toBe(false);
    expect(validateTileUrlTemplate('https://tiles.example/{z}/{x}.png').ok).toBe(false);
  });

  it('decodes supported RGB DEM pixels', () => {
    expect(decodeRgbDemPixel(128, 0, 0, 'terrarium')).toBe(0);
    expect(decodeRgbDemPixel(1, 134, 160, 'mapbox')).toBeCloseTo(0, 5);
  });

  it('normalizes malformed or missing map cache metadata as unavailable', async () => {
    const dir = await mkdtemp(path.join(os.tmpdir(), 'animus-map-cache-'));
    const manager = new MapCacheManager(dir);
    expect(await manager.status()).toMatchObject({ available: false, sets: [] });
    await mkdir(path.join(dir, 'map-cache'), { recursive: true });
    await writeFile(path.join(dir, 'map-cache', 'index.json'), 'not-json', 'utf8');
    expect(await manager.status()).toMatchObject({ available: false, sets: [] });
  });

  it('keeps satellite and DEM cache indexes independent', async () => {
    const dir = await mkdtemp(path.join(os.tmpdir(), 'animus-dem-cache-'));
    const satellite = new MapCacheManager(dir);
    const dem = new MapCacheManager(dir, 'dem');

    expect(await satellite.status()).toMatchObject({ available: false, sets: [] });
    expect(await dem.status()).toMatchObject({ available: false, sets: [] });
  });
});
