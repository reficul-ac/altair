import { expect, type Locator, type Page } from '@playwright/test';
import path from 'node:path';

export const artifactDir = process.env.ANIMUS_ARTIFACT_DIR ?? '../../artifacts/animus-interactions/local';

export async function openAnimus(page: Page): Promise<void> {
  await page.goto(process.env.ANIMUS_BASE_URL ?? '/');
  await expect(page.locator('.shell')).toBeVisible();
}

export async function selectWorkspace(page: Page, workspace: string): Promise<Locator> {
  const tab = page.locator(`[data-workspace="${workspace}"]`);
  await tab.click();
  await page.evaluate(() => window.scrollTo(0, 0));
  await expect(tab).toHaveClass(/active/);
  const panel = page.locator(`[data-panel="${workspace}"]`);
  await expect(panel).toHaveClass(/workspace-visible/);
  return panel;
}

export async function waitForLiveTelemetry(page: Page): Promise<void> {
  await expect(page.locator('#status')).toContainText(/Connected|Listening for MAVLink/, {
    timeout: 30_000
  });
  await expect(page.locator('#vehicle-id')).not.toHaveText(/--/, { timeout: 30_000 });
  await expect(page.locator('#packet')).not.toHaveText('--', { timeout: 30_000 });
}

export async function captureCheckpoint(page: Page, name: string): Promise<string> {
  const safeName = name.replace(/[^a-z0-9._-]+/gi, '-').toLowerCase();
  const filePath = path.join(artifactDir, 'screenshots', `${safeName}.png`);
  await page.screenshot({ path: filePath, fullPage: true });
  await page.evaluate((checkpoint) => {
    const target = window as typeof window & { __animusInteractionCheckpoints?: string[] };
    target.__animusInteractionCheckpoints = target.__animusInteractionCheckpoints ?? [];
    target.__animusInteractionCheckpoints.push(checkpoint);
  }, filePath);
  return filePath;
}

export async function clickIfEnabled(locator: Locator): Promise<boolean> {
  if ((await locator.count()) === 0 || !(await locator.isEnabled())) return false;
  await locator.click();
  return true;
}

export async function expectDisabledWhenPresent(locator: Locator): Promise<void> {
  if ((await locator.count()) > 0) await expect(locator.first()).toBeDisabled();
}

export async function clickCurrent(page: Page, selector: string): Promise<void> {
  await page.waitForSelector(selector, { state: 'visible' });
  await page.evaluate((targetSelector) => {
    const target = document.querySelector<HTMLElement>(targetSelector);
    if (!target) throw new Error(`missing selector: ${targetSelector}`);
    target.click();
  }, selector);
}
