import { expect, test, type Page } from '@playwright/test';
import path from 'node:path';
import { clickIfEnabled, openAnimus, selectWorkspace, waitForLiveTelemetry } from './helpers';

const artifactDir = process.env.ANIMUS_ARTIFACT_DIR ?? '../../artifacts/animus-interactions/local';
const viewportLabel = (process.env.ANIMUS_AUDIT_VIEWPORT ?? process.env.ANIMUS_VIEWPORT ?? '1440x900').replace(/[^a-z0-9._-]+/gi, '-').toLowerCase();

const cameras = ['chase', 'orbit', 'top', 'side', 'fpv', 'free'] as const;
const huds = ['console', 'tactical', 'off'] as const;
const themes = ['grid', 'rez', 'snow'] as const;
const booleans = [false, true] as const;

async function auditScreenshot(page: Page, name: string): Promise<void> {
  const safeName = `${viewportLabel}-${name}`.replace(/[^a-z0-9._-]+/gi, '-').toLowerCase();
  await page.screenshot({ path: path.join(artifactDir, 'screenshots', `${safeName}.png`), fullPage: false });
}

async function clickCurrentSelector(page: Page, selector: string): Promise<void> {
  await page.waitForSelector(selector, { state: 'visible' });
  await page.evaluate((targetSelector) => {
    const target = document.querySelector<HTMLElement>(targetSelector);
    if (!target) throw new Error(`missing selector: ${targetSelector}`);
    target.click();
  }, selector);
}

async function setFlightBoolean(page: Page, selector: string, active: boolean, activeCheck: () => Promise<boolean>): Promise<void> {
  if ((await activeCheck()) !== active) {
    await page.locator(selector).click();
  }
}

async function setTheme(page: Page, theme: (typeof themes)[number]): Promise<void> {
  for (let index = 0; index < themes.length; index += 1) {
    const text = (await page.locator('#theme-toggle').textContent())?.trim().toLowerCase();
    if (text === theme) return;
    await page.locator('#theme-toggle').click();
  }
  await expect(page.locator('#theme-toggle')).toHaveText(new RegExp(theme, 'i'));
}

async function exerciseDashboard(page: Page): Promise<void> {
  await selectWorkspace(page, 'dashboard');
  await auditScreenshot(page, 'dashboard-initial');
  await page.locator('#dashboard-add').click();
  await expect(page.locator('#dashboard-drawer .dashboard-drawer-panel')).toBeVisible();
  await auditScreenshot(page, 'dashboard-catalog-open');

  const catalogEntries = await page.locator('[data-widget-add]').evaluateAll((nodes) => nodes.map((node) => (node as HTMLElement).dataset.widgetAdd ?? '').filter(Boolean));
  for (const entry of catalogEntries) {
    await clickCurrentSelector(page, `[data-widget-add="${entry}"]`);
    await expect(page.locator(`section[data-widget-kind="${entry}"]`).last()).toBeVisible();
    await auditScreenshot(page, `dashboard-catalog-${entry}`);
  }

  const newestWidget = page.locator('section[data-widget-id]').last();
  for (const span of ['compact', 'wide', 'full']) {
    await newestWidget.locator(`[data-widget-span="${span}"]`).click();
    await expect(newestWidget).toHaveClass(new RegExp(`dashboard-span-${span}`));
    await auditScreenshot(page, `dashboard-widget-${span}`);
  }

  const widgets = page.locator('section[data-widget-id]');
  if ((await widgets.count()) >= 2) {
    await widgets.first().dragTo(widgets.last());
    await auditScreenshot(page, 'dashboard-reordered');
  }
  await newestWidget.locator('[data-widget-remove]').click();
  await auditScreenshot(page, 'dashboard-widget-removed');
  await expect(page.locator('#dashboard-import')).toBeEnabled();
  await expect(page.locator('#dashboard-export')).toBeEnabled();
  await expect(page.locator('#dashboard-reset')).toBeEnabled();
  await auditScreenshot(page, 'dashboard-profile-actions');
}

async function exerciseMap(page: Page): Promise<void> {
  await selectWorkspace(page, 'map');
  await auditScreenshot(page, 'map-initial');
  await page.locator('#map-zoom-in').click();
  await auditScreenshot(page, 'map-zoom-in');
  await page.locator('#map-zoom-out').click();
  await auditScreenshot(page, 'map-zoom-out');
  const canvas = page.locator('#map-canvas');
  await canvas.dragTo(canvas, { sourcePosition: { x: 300, y: 300 }, targetPosition: { x: 430, y: 250 } });
  await auditScreenshot(page, 'map-pan-follow-off');
  await page.locator('#map-focus').click();
  await auditScreenshot(page, 'map-focus');
}

async function exerciseInspector(page: Page): Promise<void> {
  await selectWorkspace(page, 'inspector');
  await auditScreenshot(page, 'inspector-initial');
  await page.locator('#inspector-filter').fill('ATT');
  await auditScreenshot(page, 'inspector-filter-att');
  const rows = page.locator('#inspector-table [data-message-key]');
  if ((await rows.count()) > 0) {
    await rows.first().click();
    await auditScreenshot(page, 'inspector-row-selected');
    const fields = page.locator('#chart-fields [data-chart-field]');
    if ((await fields.count()) > 0) {
      await fields.first().click();
      await auditScreenshot(page, 'inspector-chart-field');
    }
  }
  await expect(page.locator('#inspector-log-export')).toBeEnabled();
  await expect(page.locator('#inspector-export')).toBeEnabled();
  await auditScreenshot(page, 'inspector-export-actions');
}

async function exerciseVideo(page: Page): Promise<void> {
  await selectWorkspace(page, 'video');
  await expect(page.locator('[data-camera-action]')).toHaveCount(5);
  for (const action of ['capture', 'record-start', 'record-stop', 'zoom', 'focus']) {
    await expect(page.locator(`[data-camera-action="${action}"]`)).toBeDisabled();
    await auditScreenshot(page, `video-${action}-gated`);
  }
}

async function exercisePlan(page: Page): Promise<void> {
  await selectWorkspace(page, 'plan');
  await auditScreenshot(page, 'plan-initial');
  await page.locator('#plan-add').click();
  await expect(page.locator('[data-mission-field="alt_m"]').first()).toBeVisible();
  await auditScreenshot(page, 'plan-waypoint-added');
  for (const field of ['lat_deg', 'lon_deg', 'alt_m', 'throttle', 'acceptance_radius_m']) {
    const input = page.locator(`[data-mission-field="${field}"]`).first();
    await input.fill(field === 'throttle' ? '0.65' : field === 'acceptance_radius_m' ? '30' : field === 'alt_m' ? '135' : await input.inputValue());
    await input.dispatchEvent('change');
    await auditScreenshot(page, `plan-field-${field}`);
  }
  await expect(page.locator('#mission-validation')).toContainText(/Valid|warning|error/i);
  for (const selector of ['#plan-save', '#plan-restore', '#plan-upload', '#plan-download', '#plan-clear', '#terrain-request', '[data-plan-tool="geofence"]', '[data-plan-tool="rally"]']) {
    await expect(page.locator(selector)).toBeVisible();
  }
  await auditScreenshot(page, 'plan-operation-buttons');
}

async function exerciseSetup(page: Page): Promise<void> {
  await selectWorkspace(page, 'setup');
  await auditScreenshot(page, 'setup-initial');
  await expect(page.locator('#readiness-list')).toBeVisible();
  await expect(page.locator('#guarded-commands [data-command]').first()).toBeVisible();
  const commands = await page.locator('#guarded-commands [data-command]').evaluateAll((nodes) => nodes.map((node) => (node as HTMLElement).dataset.command ?? '').filter(Boolean));
  for (const command of commands) {
    await expect(page.locator(`#guarded-commands [data-command="${command}"]`)).toBeDisabled();
  }
  await auditScreenshot(page, 'setup-guarded-commands-gated');
  await page.locator('#parameter-filter').fill('SYS');
  await clickIfEnabled(page.locator('#parameter-refresh'));
  await auditScreenshot(page, 'setup-parameter-filter-refresh');
  await clickIfEnabled(page.locator('#onboard-log-list'));
  await auditScreenshot(page, 'setup-log-list');
  await expect(page.locator('#onboard-log-erase')).toBeVisible();
  await expect(page.locator('#diagnostics-list')).toBeVisible();
  await auditScreenshot(page, 'setup-diagnostics-history');
}

async function exerciseSession(page: Page): Promise<void> {
  await expect(page.locator('[data-panel="session"]')).toHaveClass(/workspace-visible/);
  await page.locator('#qgc').setChecked(false);
  await auditScreenshot(page, 'session-qgc-off');
  await page.locator('#qgc').setChecked(true);
  await page.locator('#sync-inspection').setChecked(false);
  await auditScreenshot(page, 'session-sync-off');
  for (const speed of ['0.25', '1', '2', '8']) {
    await page.locator('#replay-speed').selectOption(speed);
    await auditScreenshot(page, `session-replay-speed-${speed}`);
  }
  for (const selector of ['#replay-open', '#log-import', '#log-download', '#replay-play', '#replay-reset', '#replay-prev-marker', '#replay-next-marker']) {
    await expect(page.locator(selector)).toBeVisible();
  }
  await page.locator('#mock-count').fill('4');
  await auditScreenshot(page, 'session-mock-count');
  await page.locator('#pause').click();
  await auditScreenshot(page, 'session-paused');
  await page.locator('#clear').click();
  await page.locator('#heading-mode').click();
  await auditScreenshot(page, 'session-trail-heading');
}

test.describe('full Animus live SITL audit', () => {
  test.skip(process.env.ANIMUS_FULL_AUDIT !== '1', 'set ANIMUS_FULL_AUDIT=1 to run the full matrix audit');
  test.setTimeout(1_800_000);

  test('captures the flight visual matrix and workspace control families', async ({ page }) => {
    await openAnimus(page);
    await waitForLiveTelemetry(page);

    await selectWorkspace(page, 'flight');
    for (const theme of themes) {
      await setTheme(page, theme);
      for (const camera of cameras) {
        await page.locator(`[data-camera="${camera}"]`).click();
        await expect(page.locator(`[data-camera="${camera}"]`)).toHaveClass(/active/);
        for (const hud of huds) {
          await page.locator(`[data-hud="${hud}"]`).click();
          await expect(page.locator(`[data-hud="${hud}"]`)).toHaveClass(/active/);
          for (const ortho of booleans) {
            await setFlightBoolean(page, '#ortho-toggle', ortho, async () => !(await page.locator('#ortho').evaluate((node) => node.classList.contains('hidden'))));
            for (const debug of booleans) {
              await setFlightBoolean(page, '#debug-toggle', debug, async () => !(await page.locator('#debug').evaluate((node) => node.classList.contains('hidden'))));
              for (const locked of booleans) {
                await setFlightBoolean(page, '#camera-lock', locked, async () => await page.locator('#camera-lock').evaluate((node) => node.classList.contains('active')));
                await auditScreenshot(page, `flight-${theme}-${camera}-${hud}-ortho-${ortho ? 'on' : 'off'}-debug-${debug ? 'on' : 'off'}-lock-${locked ? 'on' : 'off'}`);
              }
            }
          }
        }
      }
    }

    await exerciseDashboard(page);
    await exerciseMap(page);
    await exerciseInspector(page);
    await exerciseVideo(page);
    await exercisePlan(page);
    await exerciseSetup(page);
    await exerciseSession(page);
  });
});
