import {
  createDefaultDashboardLayout,
  isAnimusWidgetSpan,
  moveDashboardWidget,
  setDashboardWidgetSpan,
  type AnimusDashboardLayout,
  type AnimusWidgetConfig,
  type AnimusWidgetKind
} from './dashboard-types';
import {
  DASHBOARD_COMMANDS,
  DASHBOARD_WIDGET_CATALOG,
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
      <div class="dashboard-widget-body">${body}</div>
    </section>
  `;
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
      <div class="dashboard-catalog">
        ${DASHBOARD_WIDGET_CATALOG.map((entry) => `
          <button type="button" data-widget-add="${entry.kind}">
            <strong>${escapeHtml(entry.label)}</strong>
            <span>${escapeHtml(entry.detail)}</span>
            <small>${counts.get(entry.kind) ?? 0} active</small>
          </button>
        `).join('')}
      </div>
    </div>
  `;
}
