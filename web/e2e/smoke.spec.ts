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
  await page.waitForFunction(() => typeof window.setQualityPreset === 'function', undefined, {
    timeout: 5_000,
  });
  await page.waitForFunction(
    () => document.getElementById('settings-status')?.textContent === 'Controls ready',
    undefined,
    { timeout: 30_000 },
  );
  await page.waitForFunction(
    () => {
      const chip = document.getElementById('next-sky-event');
      const text = document.getElementById('next-sky-event-text')?.textContent ?? '';
      return !!chip && !chip.hasAttribute('hidden') && /Next /i.test(text);
    },
    undefined,
    { timeout: 10_000 },
  );

  await page.waitForFunction(
    () => document.querySelectorAll('#explorer-mission-list button').length >= 1,
    undefined,
    { timeout: 20_000 },
  );
  await expect(page.locator('#explorer-missions')).not.toHaveAttribute('hidden');
  await expect(page.locator('#explorer-mission-list button')).toHaveCount(2);
  await expect(page.locator('#enter-vr')).toBeHidden();
  await expect(page.locator('#xr-hud')).toBeHidden();

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
          (line) =>
            line.includes('[StagedLoading]') && line.includes('starting download'),
        ),
      { timeout: 15_000 },
    )
    .toBe(true);

  await page.locator('#explorer-mission-list button', { hasText: 'Voyager 1' }).click();
  await page.waitForFunction(() => window.getFocusedMission?.()?.id === 'voyager1', undefined, {
    timeout: 5_000,
  });

  await page.locator('#tour-play').evaluate((el: HTMLButtonElement) => el.click());
  await expect(page.locator('#tour-caption')).not.toHaveAttribute('hidden');
  await expect(page.locator('#tour-stop')).not.toHaveAttribute('hidden');
  await page.locator('#tour-stop').evaluate((el: HTMLButtonElement) => el.click());

  const fatalConsoleErrors = consoleErrors.filter(
    (line) => !allowedConsoleErrorPatterns.some((pattern) => pattern.test(line)),
  );
  expect(fatalConsoleErrors).toEqual([]);
});
