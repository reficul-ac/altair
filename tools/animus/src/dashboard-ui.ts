import { createDefaultDashboardLayout, type AnimusDashboardLayout, type AnimusWidgetConfig, type AnimusWidgetKind } from './dashboard-types';
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
    drawerOpen: false
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
    grid?.querySelectorAll<HTMLButtonElement>('[data-dashboard-command]').forEach((button) => {
      button.addEventListener('click', () => {
        options.issueCommand(button.dataset.dashboardCommand as CommandName, 'dashboard-guarded-controls');
      });
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
    <section class="dashboard-widget dashboard-span-${widget.span}" data-widget-kind="${widget.kind}">
      <header>
        <h2>${escapeHtml(view.title)}</h2>
        <button type="button" title="Remove widget" aria-label="Remove ${escapeHtml(view.title)}" data-widget-remove="${escapeHtml(widget.id)}">x</button>
      </header>
      <div class="dashboard-widget-body">${body}</div>
    </section>
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
