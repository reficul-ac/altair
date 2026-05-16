import { mkdtemp, readFile, writeFile } from 'node:fs/promises';
import os from 'node:os';
import path from 'node:path';
import { describe, expect, it } from 'vitest';
import { createDefaultDashboardLayout } from './dashboard-types';
import { dashboardLayoutPath, readDashboardLayout, resetDashboardLayout, writeDashboardLayout } from './dashboard-settings';

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
});
