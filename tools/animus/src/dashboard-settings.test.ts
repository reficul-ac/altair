import { mkdtemp, readFile, writeFile } from 'node:fs/promises';
import os from 'node:os';
import path from 'node:path';
import { describe, expect, it } from 'vitest';
import { createDefaultDashboardLayout } from './dashboard-types';
import { dashboardLayoutPath, exportDashboardProfile, importDashboardProfile, readDashboardLayout, resetDashboardLayout, writeDashboardLayout } from './dashboard-settings';

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
});
