import path from 'node:path';
import { mkdir, readFile, writeFile } from 'node:fs/promises';

export const ANIMUS_SETTINGS_SCHEMA_VERSION = 2;
export const ANIMUS_SETTINGS_FILENAME = 'animus-settings.json';

export const ANIMUS_WORKSPACES = ['flight', 'dashboard', 'map', 'inspector', 'video', 'plan', 'setup'] as const;
export type AnimusWorkspaceName = typeof ANIMUS_WORKSPACES[number];
export const ANIMUS_THEMES = ['grid', 'rez', 'snow'] as const;
export type AnimusThemeName = typeof ANIMUS_THEMES[number];
export const ANIMUS_CAMERA_MODES = ['chase', 'orbit', 'top', 'side', 'fpv', 'free'] as const;
export type AnimusCameraMode = typeof ANIMUS_CAMERA_MODES[number];
export const ANIMUS_MAP_STYLES = ['satellite'] as const;
export type AnimusMapStyle = typeof ANIMUS_MAP_STYLES[number];

export type AnimusUiSettings = {
  schemaVersion: 2;
  defaultWorkspace: AnimusWorkspaceName;
  theme: AnimusThemeName;
  cameraMode: AnimusCameraMode;
  cameraLock: boolean;
  mapStyle: AnimusMapStyle;
  mapFollowSelected: boolean;
  satellitePmtilesPath: string | null;
  terrainPmtilesPath: string | null;
  mapPackLabel: string | null;
  mapPackAttribution: string | null;
  lastDashboardPresetLabel: string | null;
};

export function createDefaultAnimusSettings(): AnimusUiSettings {
  return {
    schemaVersion: ANIMUS_SETTINGS_SCHEMA_VERSION,
    defaultWorkspace: 'flight',
    theme: 'grid',
    cameraMode: 'chase',
    cameraLock: false,
    mapStyle: 'satellite',
    mapFollowSelected: true,
    satellitePmtilesPath: null,
    terrainPmtilesPath: null,
    mapPackLabel: null,
    mapPackAttribution: null,
    lastDashboardPresetLabel: null
  };
}

export function animusSettingsPath(userDataPath: string): string {
  return path.join(userDataPath, ANIMUS_SETTINGS_FILENAME);
}

export function normalizeAnimusSettings(value: unknown): AnimusUiSettings {
  const defaults = createDefaultAnimusSettings();
  if (!isRecord(value)) return defaults;
  if (value.schemaVersion !== ANIMUS_SETTINGS_SCHEMA_VERSION && value.schemaVersion !== 1) return defaults;
  return {
    schemaVersion: ANIMUS_SETTINGS_SCHEMA_VERSION,
    defaultWorkspace: isMember(value.defaultWorkspace, ANIMUS_WORKSPACES) ? value.defaultWorkspace : defaults.defaultWorkspace,
    theme: isMember(value.theme, ANIMUS_THEMES) ? value.theme : defaults.theme,
    cameraMode: isMember(value.cameraMode, ANIMUS_CAMERA_MODES) ? value.cameraMode : defaults.cameraMode,
    cameraLock: typeof value.cameraLock === 'boolean' ? value.cameraLock : defaults.cameraLock,
    mapStyle: isMember(value.mapStyle, ANIMUS_MAP_STYLES) ? value.mapStyle : defaults.mapStyle,
    mapFollowSelected: typeof value.mapFollowSelected === 'boolean' ? value.mapFollowSelected : defaults.mapFollowSelected,
    satellitePmtilesPath: normalizeOptionalString(value.satellitePmtilesPath, 4096),
    terrainPmtilesPath: normalizeOptionalString(value.terrainPmtilesPath, 4096),
    mapPackLabel: normalizeOptionalString(value.mapPackLabel, 120),
    mapPackAttribution: normalizeOptionalString(value.mapPackAttribution, 240),
    lastDashboardPresetLabel: typeof value.lastDashboardPresetLabel === 'string' && value.lastDashboardPresetLabel.trim()
      ? value.lastDashboardPresetLabel.trim().slice(0, 80)
      : null
  };
}

export function parseAnimusSettingsJson(raw: string): AnimusUiSettings {
  try {
    return normalizeAnimusSettings(JSON.parse(raw));
  } catch {
    return createDefaultAnimusSettings();
  }
}

export async function readAnimusSettings(filePath: string): Promise<AnimusUiSettings> {
  try {
    return parseAnimusSettingsJson(await readFile(filePath, 'utf8'));
  } catch {
    return createDefaultAnimusSettings();
  }
}

export async function writeAnimusSettings(filePath: string, settings: AnimusUiSettings): Promise<AnimusUiSettings> {
  const normalized = normalizeAnimusSettings(settings);
  await mkdir(path.dirname(filePath), { recursive: true });
  await writeFile(filePath, `${JSON.stringify(normalized, null, 2)}\n`, 'utf8');
  return normalized;
}

function isMember<T extends readonly string[]>(value: unknown, options: T): value is T[number] {
  return typeof value === 'string' && (options as readonly string[]).includes(value);
}

function normalizeOptionalString(value: unknown, maxLength: number): string | null {
  return typeof value === 'string' && value.trim() ? value.trim().slice(0, maxLength) : null;
}

function isRecord(value: unknown): value is Record<string, unknown> {
  return typeof value === 'object' && value !== null;
}
