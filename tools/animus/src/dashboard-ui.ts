import {
  createDefaultDashboardLayout,
  DASHBOARD_PRESETS,
  createDashboardPresetLayout,
  isAnimusWidgetSpan,
  moveDashboardWidget,
  setDashboardWidgetConfig,
  setDashboardWidgetSpan,
  type AnimusDashboardLayout,
  type AnimusWidgetConfig,
  type AnimusWidgetKind,
  type AnimusWidgetRuntimeConfig
} from './dashboard-types';
import {
  DASHBOARD_COMMANDS,
  DASHBOARD_WIDGET_CATALOG_GROUPS,
  buildDashboardWidgetView,
  catalogEntry,
  dashboardCommandDisabledReason,
  escapeHtml,
  renderDashboardWidgetRows,
  selectedDashboardVehicle
} from './dashboard-widgets';
import type { CommandName, GuardedCommandRequest, GuardedCommandResult, CommandDispatchResult, SessionSnapshotMessage } from './state';

type DashboardApi = {
  getDashboardLayout?: () => Promise<AnimusDashboardLayout>;
  saveDashboardLayout?: (layout: AnimusDashboardLayout) => Promise<AnimusDashboardLayout>;
  resetDashboardLayout?: () => Promise<AnimusDashboardLayout>;
  exportDashboardProfile?: (layout: AnimusDashboardLayout) => Promise<{ saved: boolean; path?: string }>;
  importDashboardProfile?: () => Promise<{ imported: boolean; path?: string; layout?: AnimusDashboardLayout }>;
};

export type DashboardController = {
  load: () => void;
  update: (snapshot: SessionSnapshotMessage | null) => void;
};

export type DashboardControllerOptions = {
  getSnapshot: () => SessionSnapshotMessage | null;
  getApi: () => DashboardApi | undefined;
  issueCommand: (command: CommandName, originSurface: string) => void;
  setStatus: (message: string) => void;
  onPresetApplied?: (label: string) => void;
};

export function createDashboardController(options: DashboardControllerOptions): DashboardController {
  const state = {
    layout: createDefaultDashboardLayout(),
    snapshot: null as SessionSnapshotMessage | null,
    drawerOpen: false,
    draggingWidgetId: null as string | null
  };
  const grid = document.querySelector<HTMLElement>('#dashboard-grid');
  const drawer = document.querySelector<HTMLElement>('#dashboard-drawer');

  function render(): void {
    if (!grid || !drawer) return;
    const snapshot = state.snapshot ?? options.getSnapshot();
    grid.innerHTML = state.layout.widgets.map((widget) => renderWidget(widget, snapshot)).join('');
    drawer.innerHTML = renderDrawer(state.layout, state.drawerOpen);
    bindGrid();
    bindDrawer();
  }

  function persist(layout: AnimusDashboardLayout): void {
    state.layout = layout;
    render();
    const save = options.getApi()?.saveDashboardLayout?.(layout);
    if (!save) return;
    void save.then((saved) => {
      state.layout = saved;
      render();
    }).catch(() => options.setStatus('Dashboard layout save failed'));
  }

  function bindGrid(): void {
    grid?.querySelectorAll<HTMLButtonElement>('[data-widget-remove]').forEach((button) => {
      button.addEventListener('click', () => {
        const id = button.dataset.widgetRemove ?? '';
        persist({ ...state.layout, widgets: state.layout.widgets.filter((widget) => widget.id !== id) });
      });
    });
    grid?.querySelectorAll<HTMLButtonElement>('[data-widget-span]').forEach((button) => {
      button.addEventListener('click', () => {
        const id = button.dataset.widgetId ?? '';
        const span = button.dataset.widgetSpan;
        if (!isAnimusWidgetSpan(span)) return;
        persist(setDashboardWidgetSpan(state.layout, id, span));
      });
    });
    grid?.querySelectorAll<HTMLSelectElement>('[data-widget-config]').forEach((select) => {
      select.addEventListener('change', () => {
        const id = select.dataset.widgetId ?? '';
        const widget = state.layout.widgets.find((candidate) => candidate.id === id);
        if (!widget) return;
        persist(setDashboardWidgetConfig(state.layout, id, {
          ...(widget.config ?? {}),
          [select.dataset.widgetConfig ?? '']: select.value
        }));
      });
    });
    grid?.querySelectorAll<HTMLInputElement>('[data-widget-threshold]').forEach((input) => {
      input.addEventListener('change', () => {
        const id = input.dataset.widgetId ?? '';
        const widget = state.layout.widgets.find((candidate) => candidate.id === id);
        if (!widget) return;
        const key = input.dataset.widgetThreshold ?? '';
        persist(setDashboardWidgetConfig(state.layout, id, {
          ...(widget.config ?? {}),
          thresholds: {
            ...(widget.config?.thresholds ?? {}),
            [key]: Number(input.value)
          }
        }));
      });
    });
    grid?.querySelectorAll<HTMLButtonElement>('[data-dashboard-command]').forEach((button) => {
      button.addEventListener('click', () => {
        options.issueCommand(button.dataset.dashboardCommand as CommandName, 'dashboard-guarded-controls');
      });
    });
    grid?.querySelectorAll<HTMLElement>('[data-widget-id]').forEach((widgetElement) => {
      widgetElement.addEventListener('dragstart', (event) => {
        const id = widgetElement.dataset.widgetId ?? '';
        state.draggingWidgetId = id;
        widgetElement.classList.add('dashboard-widget-dragging');
        grid.classList.add('dashboard-grid-dragging');
        event.dataTransfer?.setData('text/plain', id);
        if (event.dataTransfer) event.dataTransfer.effectAllowed = 'move';
      });
      widgetElement.addEventListener('dragend', () => {
        state.draggingWidgetId = null;
        clearDropIndicators();
      });
      widgetElement.addEventListener('dragover', (event) => {
        if (!state.draggingWidgetId) return;
        event.preventDefault();
        clearDropIndicators();
        widgetElement.classList.add('dashboard-widget-drop-before');
        if (event.dataTransfer) event.dataTransfer.dropEffect = 'move';
      });
      widgetElement.addEventListener('dragleave', () => {
        widgetElement.classList.remove('dashboard-widget-drop-before');
      });
      widgetElement.addEventListener('drop', (event) => {
        if (!state.draggingWidgetId) return;
        event.preventDefault();
        const beforeId = widgetElement.dataset.widgetId ?? '';
        persist(moveDashboardWidget(state.layout, state.draggingWidgetId, beforeId));
        state.draggingWidgetId = null;
        clearDropIndicators();
      });
    });
    if (grid && grid.dataset.dashboardDropBound !== 'true') {
      grid.dataset.dashboardDropBound = 'true';
      grid.addEventListener('dragover', (event) => {
        if (!state.draggingWidgetId) return;
        event.preventDefault();
        if (event.target === grid) {
          clearDropIndicators();
          grid.classList.add('dashboard-grid-drop-end');
        }
        if (event.dataTransfer) event.dataTransfer.dropEffect = 'move';
      });
      grid.addEventListener('dragleave', (event) => {
        if (!grid.contains(event.relatedTarget as Node | null)) clearDropIndicators();
      });
      grid.addEventListener('drop', (event) => {
        if (!state.draggingWidgetId) return;
        event.preventDefault();
        if (event.target === grid) persist(moveDashboardWidget(state.layout, state.draggingWidgetId, null));
        state.draggingWidgetId = null;
        clearDropIndicators();
      });
    }
  }

  function clearDropIndicators(): void {
    grid?.classList.remove('dashboard-grid-dragging', 'dashboard-grid-drop-end');
    grid?.querySelectorAll<HTMLElement>('.dashboard-widget-dragging, .dashboard-widget-drop-before').forEach((element) => {
      element.classList.remove('dashboard-widget-dragging', 'dashboard-widget-drop-before');
    });
  }

  function bindDrawer(): void {
    const addButton = document.querySelector<HTMLButtonElement>('#dashboard-add');
    if (addButton) addButton.onclick = () => {
      state.drawerOpen = !state.drawerOpen;
      render();
    };
    const resetButton = document.querySelector<HTMLButtonElement>('#dashboard-reset');
    if (resetButton) resetButton.onclick = () => {
      const reset = options.getApi()?.resetDashboardLayout?.();
      if (!reset) {
        state.layout = createDefaultDashboardLayout();
        state.drawerOpen = false;
        render();
        return;
      }
      void reset.then((layout) => {
        state.layout = layout;
        state.drawerOpen = false;
        render();
      }).catch(() => {
        state.layout = createDefaultDashboardLayout();
        state.drawerOpen = false;
        render();
      });
    };
    const importButton = document.querySelector<HTMLButtonElement>('#dashboard-import');
    if (importButton) importButton.onclick = () => {
      const importProfile = options.getApi()?.importDashboardProfile?.();
      if (!importProfile) {
        options.setStatus('Dashboard profile import is unavailable in this runtime');
        return;
      }
      void importProfile.then((result) => {
        if (!result.imported || !result.layout) {
          options.setStatus('Dashboard profile import canceled');
          return;
        }
        state.drawerOpen = false;
        persist(result.layout);
        options.setStatus(`Dashboard profile imported${result.path ? ` from ${result.path}` : ''}`);
      }).catch(() => options.setStatus('Dashboard profile import failed'));
    };
    const exportButton = document.querySelector<HTMLButtonElement>('#dashboard-export');
    if (exportButton) exportButton.onclick = () => {
      const exportProfile = options.getApi()?.exportDashboardProfile?.(state.layout);
      if (!exportProfile) {
        options.setStatus('Dashboard profile export is unavailable in this runtime');
        return;
      }
      void exportProfile.then((result) => {
        options.setStatus(result.saved
          ? `Dashboard profile exported${result.path ? ` to ${result.path}` : ''}`
          : 'Dashboard profile export canceled');
      }).catch(() => options.setStatus('Dashboard profile export failed'));
    };
    drawer?.querySelectorAll<HTMLButtonElement>('[data-dashboard-preset]').forEach((button) => {
      button.addEventListener('click', () => {
        const preset = DASHBOARD_PRESETS.find((candidate) => candidate.id === button.dataset.dashboardPreset);
        if (!preset) return;
        if (!window.confirm(`Replace dashboard layout with ${preset.label}?`)) return;
        state.drawerOpen = false;
        persist(createDashboardPresetLayout(preset.id));
        options.onPresetApplied?.(preset.label);
        options.setStatus(`${preset.label} dashboard preset applied`);
      });
    });
    drawer?.querySelectorAll<HTMLButtonElement>('[data-widget-add]').forEach((button) => {
      button.addEventListener('click', () => {
        const kind = button.dataset.widgetAdd as AnimusWidgetKind;
        const entry = catalogEntry(kind);
        const next: AnimusWidgetConfig = {
          id: `${kind}-${Date.now().toString(36)}`,
          kind,
          span: entry.span
        };
        persist({ ...state.layout, widgets: [...state.layout.widgets, next] });
      });
    });
  }

  return {
    load() {
      bindDrawer();
      const load = options.getApi()?.getDashboardLayout?.();
      if (!load) {
        render();
        return;
      }
      void load.then((layout) => {
        state.layout = layout;
        render();
      }).catch(() => render());
      render();
    },
    update(snapshot) {
      state.snapshot = snapshot;
      render();
    }
  };
}

function renderWidget(widget: AnimusWidgetConfig, snapshot: SessionSnapshotMessage | null): string {
  const view = buildDashboardWidgetView(widget, snapshot);
  const configControls = renderWidgetConfigControls(widget);
  const body = widget.kind === 'guarded-controls'
    ? `${renderDashboardWidgetRows(view)}${renderCommandButtons(snapshot)}`
    : renderDashboardWidgetRows(view);
  return `
    <section class="dashboard-widget dashboard-span-${widget.span}" data-widget-kind="${widget.kind}" data-widget-id="${escapeHtml(widget.id)}" draggable="true">
      <header>
        <span class="dashboard-drag-handle" title="Drag to reorder" aria-hidden="true">::::</span>
        <h2>${escapeHtml(view.title)}</h2>
        <div class="dashboard-widget-controls">
          ${renderSpanButtons(widget)}
          <button type="button" title="Remove widget" aria-label="Remove ${escapeHtml(view.title)}" data-widget-remove="${escapeHtml(widget.id)}">x</button>
        </div>
      </header>
      ${configControls}
      <div class="dashboard-widget-body">${body}</div>
    </section>
  `;
}

function renderWidgetConfigControls(widget: AnimusWidgetConfig): string {
  const scope = widget.config?.vehicleScope ?? 'selected';
  const configurableScope = widget.kind !== 'guarded-controls';
  const thresholdControls = widget.kind === 'link-freshness' ? renderThresholdControls(widget.config, widget.id) : '';
  if (!configurableScope && !thresholdControls) return '';
  return `
    <div class="dashboard-widget-config">
      ${configurableScope ? `
        <label>
          <span>Scope</span>
          <select data-widget-id="${escapeHtml(widget.id)}" data-widget-config="vehicleScope">
            <option value="selected" ${scope === 'selected' ? 'selected' : ''}>Selected</option>
            <option value="fleet" ${scope === 'fleet' ? 'selected' : ''}>Fleet</option>
          </select>
        </label>
      ` : ''}
      ${renderUnitsControl(widget)}
      ${thresholdControls}
    </div>
  `;
}

function renderUnitsControl(widget: AnimusWidgetConfig): string {
  if (widget.kind !== 'gps-battery' && widget.kind !== 'position-velocity' && widget.kind !== 'mission-progress' && widget.kind !== 'attitude-summary') return '';
  return `
    <label>
      <span>Units</span>
      <select data-widget-id="${escapeHtml(widget.id)}" data-widget-config="units">
        <option value="metric" selected>Metric</option>
      </select>
    </label>
  `;
}

function renderThresholdControls(config: AnimusWidgetRuntimeConfig | undefined, widgetId: string): string {
  const thresholds = config?.thresholds ?? {};
  const fields = [
    { key: 'packetWarningS', label: 'Pkt warn', value: thresholds.packetWarningS ?? 1 },
    { key: 'packetDangerS', label: 'Pkt danger', value: thresholds.packetDangerS ?? 2 },
    { key: 'heartbeatWarningS', label: 'HB warn', value: thresholds.heartbeatWarningS ?? 1 },
    { key: 'heartbeatDangerS', label: 'HB danger', value: thresholds.heartbeatDangerS ?? 2 }
  ] as const;
  return fields.map((field) => `
    <label>
      <span>${field.label}</span>
      <input
        data-widget-id="${escapeHtml(widgetId)}"
        data-widget-threshold="${field.key}"
        type="number"
        min="0"
        max="3600"
        step="0.25"
        value="${field.value}"
      />
    </label>
  `).join('');
}

function renderSpanButtons(widget: AnimusWidgetConfig): string {
  const spans = [
    { value: 'compact', label: 'C', title: 'Compact width' },
    { value: 'wide', label: 'W', title: 'Wide width' },
    { value: 'full', label: 'F', title: 'Full width' }
  ] as const;
  return `
    <div class="dashboard-span-controls" aria-label="Widget width">
      ${spans.map((span) => `
        <button
          type="button"
          class="${widget.span === span.value ? 'active' : ''}"
          title="${span.title}"
          aria-label="${span.title}"
          aria-pressed="${widget.span === span.value ? 'true' : 'false'}"
          data-widget-id="${escapeHtml(widget.id)}"
          data-widget-span="${span.value}"
        >${span.label}</button>
      `).join('')}
    </div>
  `;
}

function renderCommandButtons(snapshot: SessionSnapshotMessage | null): string {
  const vehicle = selectedDashboardVehicle(snapshot);
  return `
    <div class="dashboard-command-bank">
      ${DASHBOARD_COMMANDS.map((command) => {
        const reason = dashboardCommandDisabledReason(vehicle, command);
        return `<button type="button" data-dashboard-command="${command}" ${reason ? 'disabled' : ''} title="${escapeHtml(reason ?? `Send ${command}`)}">${escapeHtml(command)}</button>`;
      }).join('')}
    </div>
  `;
}

function renderDrawer(layout: AnimusDashboardLayout, open: boolean): string {
  const counts = new Map<AnimusWidgetKind, number>();
  layout.widgets.forEach((widget) => counts.set(widget.kind, (counts.get(widget.kind) ?? 0) + 1));
  return `
    <div class="dashboard-drawer-panel ${open ? '' : 'hidden'}">
      <div class="dashboard-presets">
        ${DASHBOARD_PRESETS.map((preset) => `
          <button type="button" data-dashboard-preset="${preset.id}">
            <strong>${escapeHtml(preset.label)}</strong>
            <span>${preset.layout.widgets.length} widgets</span>
          </button>
        `).join('')}
      </div>
      <div class="dashboard-catalog">
        ${DASHBOARD_WIDGET_CATALOG_GROUPS.map((group) => `
          <section class="dashboard-catalog-group">
            <h2>${escapeHtml(group.label)}</h2>
            ${group.kinds.map((kind) => {
              const entry = catalogEntry(kind);
              return `
                <button type="button" data-widget-add="${entry.kind}">
                  <strong>${escapeHtml(entry.label)}</strong>
                  <span>${escapeHtml(entry.detail)}</span>
                  <small>${counts.get(entry.kind) ?? 0} active</small>
                </button>
              `;
            }).join('')}
          </section>
        `).join('')}
      </div>
    </div>
  `;
}
