import { describe, expect, it } from 'vitest';
import { ANIMUS_WIDGET_KINDS, createDefaultDashboardLayout, normalizeDashboardLayout, parseDashboardLayoutJson } from './dashboard-types';

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
});
