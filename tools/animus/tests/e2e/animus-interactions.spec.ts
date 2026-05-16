import { expect, test } from '@playwright/test';
import {
  captureCheckpoint,
  clickCurrent,
  clickIfEnabled,
  expectDisabledWhenPresent,
  openAnimus,
  selectWorkspace,
  waitForLiveTelemetry
} from './helpers';

test.describe('live SITL Animus interactions', () => {
  test('drives the core workspace controls and captures checkpoints', async ({ page }) => {
    await openAnimus(page);
    await waitForLiveTelemetry(page);

    await selectWorkspace(page, 'flight');
    await page.getByRole('button', { name: 'Orbit' }).click();
    await page.getByRole('button', { name: 'Tactical' }).click();
    await page.locator('#ortho-toggle').click();
    await page.locator('#debug-toggle').click();
    await page.locator('#theme-toggle').click();
    await expect(page.locator('#hud-tactical')).toBeVisible();
    await captureCheckpoint(page, '01-flight-controls');

    await selectWorkspace(page, 'dashboard');
    await page.locator('#dashboard-add').click();
    await expect(page.locator('#dashboard-drawer .dashboard-drawer-panel')).toBeVisible();
    const initialWidgetCount = await page.locator('section[data-widget-id]').count();
    await clickCurrent(page, '[data-widget-add="guarded-controls"]');
    await expect(page.locator('section[data-widget-id]')).toHaveCount(initialWidgetCount + 1);
    await expect(page.locator('section[data-widget-id]')).not.toHaveCount(0);
    const newestWidget = page.locator('section[data-widget-id]').last();
    await clickCurrent(page, 'section[data-widget-id]:last-of-type [data-widget-span="full"]');
    await expect(newestWidget).toHaveClass(/dashboard-span-full/);
    await captureCheckpoint(page, '02-dashboard-widget-added');
    await clickCurrent(page, 'section[data-widget-id]:last-of-type [data-widget-remove]');
    await captureCheckpoint(page, '03-dashboard-widget-removed');

    await selectWorkspace(page, 'map');
    await page.locator('#map-zoom-in').click();
    await page.locator('#map-zoom-out').click();
    await page.locator('#map-focus').click();
    await captureCheckpoint(page, '04-map-controls');

    await selectWorkspace(page, 'inspector');
    await page.locator('#inspector-filter').fill('ATT');
    const firstMessage = page.locator('#inspector-table [data-message-key]').first();
    if ((await firstMessage.count()) > 0) {
      await firstMessage.click();
      const firstChartField = page.locator('#chart-fields [data-chart-field]').first();
      await clickIfEnabled(firstChartField);
    }
    await captureCheckpoint(page, '05-inspector-filter');

    await selectWorkspace(page, 'video');
    await expect(page.locator('[data-camera-action]')).toHaveCount(5);
    await expectDisabledWhenPresent(page.locator('[data-camera-action="capture"]'));
    await captureCheckpoint(page, '06-video-gated-controls');

    await selectWorkspace(page, 'plan');
    await clickCurrent(page, '#plan-add');
    await expect(page.locator('[data-mission-field="alt_m"]').first()).toBeVisible();
    await page.locator('[data-mission-field="alt_m"]').first().fill('135');
    await page.locator('[data-mission-field="alt_m"]').first().dispatchEvent('change');
    await expect(page.locator('#mission-validation')).toContainText(/Valid|warning|error/);
    await captureCheckpoint(page, '07-plan-waypoint');

    await selectWorkspace(page, 'setup');
    await page.locator('#parameter-filter').fill('SYS');
    await clickIfEnabled(page.locator('#parameter-refresh'));
    await clickIfEnabled(page.locator('#onboard-log-list'));
    await expect(page.locator('#guarded-commands [data-command]').first()).toBeVisible();
    await expectDisabledWhenPresent(page.locator('#guarded-commands [data-command="arm"]'));
    await captureCheckpoint(page, '08-setup-gated-controls');

    await expect(page.locator('[data-panel="session"]')).toHaveClass(/workspace-visible/);
    const qgc = page.locator('#qgc');
    if (await qgc.isEnabled()) {
      await qgc.setChecked(!(await qgc.isChecked()));
    }
    await page.locator('#sync-inspection').setChecked(false);
    await page.locator('#replay-speed').selectOption('2');
    await captureCheckpoint(page, '09-session-controls');
  });
});
