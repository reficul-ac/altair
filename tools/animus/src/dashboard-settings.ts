import { mkdir, readFile, writeFile } from 'node:fs/promises';
import path from 'node:path';
import { createDefaultDashboardLayout, normalizeDashboardLayout, parseDashboardLayoutJson, type AnimusDashboardLayout } from './dashboard-types.js';

export const DASHBOARD_LAYOUT_FILENAME = 'dashboard-layout.json';

export function dashboardLayoutPath(userDataPath: string): string {
  return path.join(userDataPath, DASHBOARD_LAYOUT_FILENAME);
}

export async function readDashboardLayout(filePath: string): Promise<AnimusDashboardLayout> {
  try {
    return parseDashboardLayoutJson(await readFile(filePath, 'utf8'));
  } catch {
    return createDefaultDashboardLayout();
  }
}

export async function writeDashboardLayout(filePath: string, layout: AnimusDashboardLayout): Promise<AnimusDashboardLayout> {
  const normalized = normalizeDashboardLayout(layout);
  await mkdir(path.dirname(filePath), { recursive: true });
  await writeFile(filePath, `${JSON.stringify(normalized, null, 2)}\n`, 'utf8');
  return normalized;
}

export async function resetDashboardLayout(filePath: string): Promise<AnimusDashboardLayout> {
  return writeDashboardLayout(filePath, createDefaultDashboardLayout());
}

export async function exportDashboardProfile(filePath: string, layout: AnimusDashboardLayout): Promise<AnimusDashboardLayout> {
  return writeDashboardLayout(filePath, layout);
}

export async function importDashboardProfile(filePath: string): Promise<AnimusDashboardLayout> {
  return readDashboardLayout(filePath);
}
