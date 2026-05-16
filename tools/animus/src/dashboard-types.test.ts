import { describe, expect, it } from 'vitest';
import {
  ANIMUS_WIDGET_KINDS,
  createDefaultDashboardLayout,
  moveDashboardWidget,
  normalizeDashboardLayout,
  parseDashboardLayoutJson,
  setDashboardWidgetSpan
} from './dashboard-types';

describe('dashboard layout validation', () => {
  it('loads a valid saved layout', () => {
    const layout = normalizeDashboardLayout({
      schemaVersion: 1,
      widgets: [{ id: 'link', kind: 'link-freshness', span: 'wide' }]
    });

    expect(layout.widgets).toEqual([{ id: 'link', kind: 'link-freshness', span: 'wide' }]);
  });

  it('drops unknown widget kinds', () => {
    const layout = normalizeDashboardLayout({
      schemaVersion: 1,
      widgets: [
        { id: 'good', kind: 'gps-battery', span: 'compact' },
        { id: 'bad', kind: 'unknown-widget', span: 'full' }
      ]
    });

    expect(layout.widgets.map((widget) => widget.kind)).toEqual(['gps-battery']);
  });

  it('normalizes duplicate widget ids', () => {
    const layout = normalizeDashboardLayout({
      schemaVersion: 1,
      widgets: [
        { id: 'dup', kind: 'link-freshness', span: 'compact' },
        { id: 'dup', kind: 'identity-mode', span: 'compact' },
        { id: 'dup', kind: 'gps-battery', span: 'compact' }
      ]
    });

    expect(layout.widgets.map((widget) => widget.id)).toEqual(['dup', 'dup-2', 'dup-3']);
  });

  it('falls back to defaults for malformed json', () => {
    const layout = parseDashboardLayoutJson('{not-json');

    expect(layout).toEqual(createDefaultDashboardLayout());
    expect(layout.widgets.map((widget) => widget.kind)).toEqual([...ANIMUS_WIDGET_KINDS]);
  });

  it('moves a widget before another widget', () => {
    const layout = normalizeDashboardLayout({
      schemaVersion: 1,
      widgets: [
        { id: 'link', kind: 'link-freshness', span: 'compact' },
        { id: 'identity', kind: 'identity-mode', span: 'compact' },
        { id: 'battery', kind: 'gps-battery', span: 'compact' }
      ]
    });

    const moved = moveDashboardWidget(layout, 'battery', 'identity');

    expect(moved.widgets.map((widget) => widget.id)).toEqual(['link', 'battery', 'identity']);
  });

  it('moves a widget to the end', () => {
    const layout = normalizeDashboardLayout({
      schemaVersion: 1,
      widgets: [
        { id: 'link', kind: 'link-freshness', span: 'compact' },
        { id: 'identity', kind: 'identity-mode', span: 'compact' },
        { id: 'battery', kind: 'gps-battery', span: 'compact' }
      ]
    });

    const moved = moveDashboardWidget(layout, 'link', null);

    expect(moved.widgets.map((widget) => widget.id)).toEqual(['identity', 'battery', 'link']);
  });

  it('leaves normalized layout unchanged for invalid widget moves', () => {
    const layout = {
      schemaVersion: 1,
      widgets: [
        { id: 'dup', kind: 'link-freshness', span: 'compact' },
        { id: 'dup', kind: 'identity-mode', span: 'bad' }
      ]
    } as const;
    const normalized = normalizeDashboardLayout(layout);

    expect(moveDashboardWidget(layout, 'missing', 'dup')).toEqual(normalized);
    expect(moveDashboardWidget(layout, 'dup', 'missing')).toEqual(normalized);
  });

  it('updates widget spans only for supported span values', () => {
    const layout = normalizeDashboardLayout({
      schemaVersion: 1,
      widgets: [{ id: 'link', kind: 'link-freshness', span: 'compact' }]
    });

    expect(setDashboardWidgetSpan(layout, 'link', 'wide').widgets[0]?.span).toBe('wide');
    expect(setDashboardWidgetSpan(layout, 'link', 'full').widgets[0]?.span).toBe('full');
    expect(setDashboardWidgetSpan(layout, 'link', 'compact').widgets[0]?.span).toBe('compact');
    expect(setDashboardWidgetSpan(layout, 'link', 'tall')).toEqual(layout);
  });
});
