export const DASHBOARD_SCHEMA_VERSION = 1;

export const ANIMUS_WIDGET_KINDS = [
  'link-freshness',
  'identity-mode',
  'arm-failsafe-readiness',
  'gps-battery',
  'attitude-summary',
  'position-velocity',
  'mission-progress',
  'status-events',
  'guarded-controls'
] as const;

export type AnimusWidgetKind = typeof ANIMUS_WIDGET_KINDS[number];

export type AnimusWidgetSpan = 'compact' | 'wide' | 'full';

export type AnimusWidgetVehicleScope = 'selected' | 'fleet';

export type AnimusWidgetUnits = 'metric';

export type AnimusWidgetThresholds = {
  packetWarningS?: number;
  packetDangerS?: number;
  heartbeatWarningS?: number;
  heartbeatDangerS?: number;
};

export type AnimusWidgetRuntimeConfig = {
  vehicleScope?: AnimusWidgetVehicleScope;
  units?: AnimusWidgetUnits;
  thresholds?: AnimusWidgetThresholds;
};

export type AnimusWidgetConfig = {
  id: string;
  kind: AnimusWidgetKind;
  span: AnimusWidgetSpan;
  config?: AnimusWidgetRuntimeConfig;
};

export type AnimusDashboardLayout = {
  schemaVersion: 1;
  widgets: AnimusWidgetConfig[];
};

export const DASHBOARD_PRESET_IDS = ['flight-test', 'mission-planning', 'maintenance'] as const;

export type AnimusDashboardPresetId = typeof DASHBOARD_PRESET_IDS[number];

export type AnimusDashboardPreset = {
  id: AnimusDashboardPresetId;
  label: string;
  layout: AnimusDashboardLayout;
};

const widgetKindSet = new Set<string>(ANIMUS_WIDGET_KINDS);
const widgetSpanSet = new Set<string>(['compact', 'wide', 'full']);
const widgetVehicleScopeSet = new Set<string>(['selected', 'fleet']);
const widgetUnitsSet = new Set<string>(['metric']);

export function defaultWidgetSpan(kind: AnimusWidgetKind): AnimusWidgetSpan {
  return kind === 'guarded-controls' || kind === 'status-events' ? 'full' : 'compact';
}

export function createDefaultDashboardLayout(): AnimusDashboardLayout {
  return {
    schemaVersion: DASHBOARD_SCHEMA_VERSION,
    widgets: ANIMUS_WIDGET_KINDS.map((kind) => ({
      id: kind,
      kind,
      span: defaultWidgetSpan(kind)
    }))
  };
}

export function isAnimusWidgetKind(value: unknown): value is AnimusWidgetKind {
  return typeof value === 'string' && widgetKindSet.has(value);
}

export function isAnimusWidgetSpan(value: unknown): value is AnimusWidgetSpan {
  return typeof value === 'string' && widgetSpanSet.has(value);
}

export function isAnimusWidgetVehicleScope(value: unknown): value is AnimusWidgetVehicleScope {
  return typeof value === 'string' && widgetVehicleScopeSet.has(value);
}

export function isAnimusWidgetUnits(value: unknown): value is AnimusWidgetUnits {
  return typeof value === 'string' && widgetUnitsSet.has(value);
}

export function normalizeDashboardLayout(value: unknown): AnimusDashboardLayout {
  if (!isRecord(value) || value.schemaVersion !== DASHBOARD_SCHEMA_VERSION || !Array.isArray(value.widgets)) {
    return createDefaultDashboardLayout();
  }

  const ids = new Set<string>();
  const widgets: AnimusWidgetConfig[] = [];
  for (const raw of value.widgets) {
    if (!isRecord(raw) || !isAnimusWidgetKind(raw.kind)) continue;
    const baseId = sanitizeWidgetId(typeof raw.id === 'string' ? raw.id : raw.kind);
    const id = uniqueWidgetId(baseId || raw.kind, ids);
    const span = isAnimusWidgetSpan(raw.span) ? raw.span : defaultWidgetSpan(raw.kind);
    const config = normalizeWidgetRuntimeConfig(raw.config);
    widgets.push(config ? { id, kind: raw.kind, span, config } : { id, kind: raw.kind, span });
  }

  return widgets.length > 0 ? { schemaVersion: DASHBOARD_SCHEMA_VERSION, widgets } : createDefaultDashboardLayout();
}

export function parseDashboardLayoutJson(raw: string): AnimusDashboardLayout {
  try {
    return normalizeDashboardLayout(JSON.parse(raw));
  } catch {
    return createDefaultDashboardLayout();
  }
}

export function moveDashboardWidget(layout: unknown, widgetId: string, beforeWidgetId: string | null): AnimusDashboardLayout {
  const normalized = normalizeDashboardLayout(layout);
  const fromIndex = normalized.widgets.findIndex((widget) => widget.id === widgetId);
  if (fromIndex < 0 || widgetId === beforeWidgetId) return normalized;

  if (beforeWidgetId !== null && !normalized.widgets.some((widget) => widget.id === beforeWidgetId)) {
    return normalized;
  }

  const widgets = [...normalized.widgets];
  const [moved] = widgets.splice(fromIndex, 1);
  if (!moved) return normalized;

  if (beforeWidgetId === null) {
    widgets.push(moved);
  } else {
    const beforeIndex = widgets.findIndex((widget) => widget.id === beforeWidgetId);
    widgets.splice(beforeIndex, 0, moved);
  }

  return { ...normalized, widgets };
}

export function setDashboardWidgetSpan(layout: unknown, widgetId: string, span: unknown): AnimusDashboardLayout {
  const normalized = normalizeDashboardLayout(layout);
  if (!isAnimusWidgetSpan(span)) return normalized;

  const widgetIndex = normalized.widgets.findIndex((widget) => widget.id === widgetId);
  if (widgetIndex < 0) return normalized;

  const widgets = normalized.widgets.map((widget, index) => (
    index === widgetIndex ? { ...widget, span } : widget
  ));
  return { ...normalized, widgets };
}

export function setDashboardWidgetConfig(layout: unknown, widgetId: string, config: unknown): AnimusDashboardLayout {
  const normalized = normalizeDashboardLayout(layout);
  const widgetIndex = normalized.widgets.findIndex((widget) => widget.id === widgetId);
  if (widgetIndex < 0) return normalized;

  const nextConfig = normalizeWidgetRuntimeConfig(config);
  const widgets = normalized.widgets.map((widget, index) => {
    if (index !== widgetIndex) return widget;
    return nextConfig ? { ...widget, config: nextConfig } : withoutConfig(widget);
  });
  return { ...normalized, widgets };
}

export function createDashboardPresetLayout(id: AnimusDashboardPresetId): AnimusDashboardLayout {
  const preset = DASHBOARD_PRESETS.find((candidate) => candidate.id === id);
  return preset ? cloneLayout(preset.layout) : createDefaultDashboardLayout();
}

export const DASHBOARD_PRESETS: AnimusDashboardPreset[] = [
  {
    id: 'flight-test',
    label: 'Flight Test',
    layout: {
      schemaVersion: DASHBOARD_SCHEMA_VERSION,
      widgets: [
        { id: 'flight-link', kind: 'link-freshness', span: 'compact', config: { vehicleScope: 'selected', thresholds: { packetWarningS: 0.75, packetDangerS: 1.5, heartbeatWarningS: 1.0, heartbeatDangerS: 2.0 } } },
        { id: 'flight-attitude', kind: 'attitude-summary', span: 'compact', config: { vehicleScope: 'selected', units: 'metric' } },
        { id: 'flight-position', kind: 'position-velocity', span: 'wide', config: { vehicleScope: 'selected', units: 'metric' } },
        { id: 'flight-readiness', kind: 'arm-failsafe-readiness', span: 'compact', config: { vehicleScope: 'selected' } },
        { id: 'flight-events', kind: 'status-events', span: 'full', config: { vehicleScope: 'fleet' } },
        { id: 'flight-controls', kind: 'guarded-controls', span: 'full', config: { vehicleScope: 'selected' } }
      ]
    }
  },
  {
    id: 'mission-planning',
    label: 'Mission Planning',
    layout: {
      schemaVersion: DASHBOARD_SCHEMA_VERSION,
      widgets: [
        { id: 'mission-identity', kind: 'identity-mode', span: 'compact', config: { vehicleScope: 'selected' } },
        { id: 'mission-progress', kind: 'mission-progress', span: 'wide', config: { vehicleScope: 'selected', units: 'metric' } },
        { id: 'mission-position', kind: 'position-velocity', span: 'wide', config: { vehicleScope: 'selected', units: 'metric' } },
        { id: 'mission-gps', kind: 'gps-battery', span: 'compact', config: { vehicleScope: 'selected', units: 'metric' } },
        { id: 'mission-events', kind: 'status-events', span: 'full', config: { vehicleScope: 'fleet' } }
      ]
    }
  },
  {
    id: 'maintenance',
    label: 'Maintenance',
    layout: {
      schemaVersion: DASHBOARD_SCHEMA_VERSION,
      widgets: [
        { id: 'maintenance-link', kind: 'link-freshness', span: 'wide', config: { vehicleScope: 'fleet', thresholds: { packetWarningS: 1.0, packetDangerS: 2.0, heartbeatWarningS: 1.0, heartbeatDangerS: 2.0 } } },
        { id: 'maintenance-identity', kind: 'identity-mode', span: 'compact', config: { vehicleScope: 'selected' } },
        { id: 'maintenance-readiness', kind: 'arm-failsafe-readiness', span: 'wide', config: { vehicleScope: 'selected' } },
        { id: 'maintenance-gps-battery', kind: 'gps-battery', span: 'compact', config: { vehicleScope: 'selected', units: 'metric' } },
        { id: 'maintenance-controls', kind: 'guarded-controls', span: 'full', config: { vehicleScope: 'selected' } },
        { id: 'maintenance-events', kind: 'status-events', span: 'full', config: { vehicleScope: 'fleet' } }
      ]
    }
  }
];

function normalizeWidgetRuntimeConfig(value: unknown): AnimusWidgetRuntimeConfig | undefined {
  if (!isRecord(value)) return undefined;
  const config: AnimusWidgetRuntimeConfig = {};
  if (isAnimusWidgetVehicleScope(value.vehicleScope)) config.vehicleScope = value.vehicleScope;
  if (isAnimusWidgetUnits(value.units)) config.units = value.units;
  const thresholds = normalizeThresholds(value.thresholds);
  if (thresholds) config.thresholds = thresholds;
  return Object.keys(config).length > 0 ? config : undefined;
}

function normalizeThresholds(value: unknown): AnimusWidgetThresholds | undefined {
  if (!isRecord(value)) return undefined;
  const thresholds: AnimusWidgetThresholds = {};
  for (const key of ['packetWarningS', 'packetDangerS', 'heartbeatWarningS', 'heartbeatDangerS'] as const) {
    const raw = value[key];
    if (typeof raw === 'number' && Number.isFinite(raw) && raw >= 0 && raw <= 3600) thresholds[key] = raw;
  }
  return Object.keys(thresholds).length > 0 ? thresholds : undefined;
}

function withoutConfig(widget: AnimusWidgetConfig): AnimusWidgetConfig {
  return { id: widget.id, kind: widget.kind, span: widget.span };
}

function cloneLayout(layout: AnimusDashboardLayout): AnimusDashboardLayout {
  return normalizeDashboardLayout(JSON.parse(JSON.stringify(layout)));
}

function uniqueWidgetId(baseId: string, ids: Set<string>): string {
  let id = baseId;
  let suffix = 2;
  while (ids.has(id)) {
    id = `${baseId}-${suffix}`;
    suffix += 1;
  }
  ids.add(id);
  return id;
}

function sanitizeWidgetId(value: string): string {
  return value.trim().replaceAll(/[^a-zA-Z0-9_-]/g, '-').slice(0, 64);
}

function isRecord(value: unknown): value is Record<string, unknown> {
  return typeof value === 'object' && value !== null;
}
