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

export type AnimusWidgetConfig = {
  id: string;
  kind: AnimusWidgetKind;
  span: AnimusWidgetSpan;
};

export type AnimusDashboardLayout = {
  schemaVersion: 1;
  widgets: AnimusWidgetConfig[];
};

const widgetKindSet = new Set<string>(ANIMUS_WIDGET_KINDS);
const widgetSpanSet = new Set<string>(['compact', 'wide', 'full']);

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
    widgets.push({ id, kind: raw.kind, span });
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
