import { expect, test } from '@playwright/test';

const allowedConsoleErrorPatterns = [
  /Failed to initialize planet explorer/i,
  /Failed to load resource\/textures_low\//i,
  /not a DDS file/i,
  /\[Audio\]/i,
  /\[Texture\] Warning/i,
];

test('WASM module boots and staged loading reacts to camera pose', async ({ page, baseURL }) => {
  const consoleErrors: string[] = [];
  const consoleLogs: string[] = [];

  page.on('console', (message) => {
    const text = message.text();
    consoleLogs.push(text);
    if (message.type() === 'error') {
      consoleErrors.push(text);
    }
  });
  page.on('pageerror', (error) => {
    consoleErrors.push(error.message);
  });

  const response = await page.goto(baseURL ?? '/', { waitUntil: 'domcontentloaded' });
  expect(response?.ok()).toBeTruthy();

  await page.waitForFunction(() => typeof window.setCameraPose === 'function', undefined, {
    timeout: 45_000,
  });
  await page.waitForFunction(
    () => document.getElementById('settings-status')?.textContent === 'Controls ready',
    undefined,
    { timeout: 30_000 },
  );

  await expect
    .poll(() => consoleLogs.some((line) => line.includes('SolarSystem WASM initialized')), {
      timeout: 15_000,
    })
    .toBe(true);
  await expect
    .poll(() => consoleLogs.some((line) => line.includes('[StagedLoading] Loaded manifest')), {
      timeout: 15_000,
    })
    .toBe(true);

  await page.locator('#canvas').hover();
  await page.evaluate(() => {
    window.setCameraPose?.(1200, 0, 350, 0, 0);
  });

  await expect
    .poll(
      () =>
        consoleLogs.some(
          (line) => line.includes('[StagedLoading]') && line.includes('Mercury'),
        ),
      { timeout: 15_000 },
    )
    .toBe(true);

  const fatalConsoleErrors = consoleErrors.filter(
    (line) => !allowedConsoleErrorPatterns.some((pattern) => pattern.test(line)),
  );
  expect(fatalConsoleErrors).toEqual([]);
});
