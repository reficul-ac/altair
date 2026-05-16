import { defineConfig, devices } from '@playwright/test';

const artifactDir = process.env.ANIMUS_ARTIFACT_DIR ?? '../../artifacts/animus-interactions/local';
const [width, height] = (process.env.ANIMUS_VIEWPORT ?? '1440x900')
  .split('x')
  .map((part) => Number(part));

export default defineConfig({
  testDir: './tests/e2e',
  fullyParallel: false,
  retries: 0,
  timeout: 90_000,
  expect: { timeout: 10_000 },
  outputDir: `${artifactDir}/playwright-results`,
  reporter: [
    ['list'],
    ['html', { outputFolder: `${artifactDir}/playwright-report`, open: 'never' }],
    ['json', { outputFile: `${artifactDir}/playwright-report/results.json` }]
  ],
  use: {
    baseURL: process.env.ANIMUS_BASE_URL ?? 'http://127.0.0.1:5173',
    viewport: {
      width: Number.isFinite(width) && width > 0 ? width : 1440,
      height: Number.isFinite(height) && height > 0 ? height : 900
    },
    screenshot: 'only-on-failure',
    trace: 'retain-on-failure',
    video: 'retain-on-failure'
  },
  projects: [
    {
      name: 'chromium',
      use: { browserName: 'chromium', userAgent: devices['Desktop Chrome'].userAgent }
    }
  ]
});
