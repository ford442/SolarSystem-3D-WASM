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

  // No planet may end up on the 4x4 fallback checkerboard. On a context without
  // WEBGL_compressed_texture_s3tc the DDS tier is software-decoded to RGBA8
  // (BlockCompression.cpp) rather than skipped, so this holds on WebKit too once a
  // webkit project is added to playwright.config.ts.
  expect(consoleLogs.filter((line) => line.includes('Created 4x4 fallback texture'))).toEqual([]);
  expect(
    consoleLogs.filter((line) => /compressed textures are not supported/i.test(line)),
  ).toEqual([]);
  expect(
    consoleLogs.some((line) => line.includes('[GlCapabilities]') && line.includes('preferredPack=')),
  ).toBe(true);

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

  // Every preset must switch cleanly: Low falls back to the O'Neil atmosphere and the flat
  // corona, Medium/Full draw the LUT atmosphere and the sliced corona (ARCHITECTURE §9.2).
  // A shader or LUT problem would surface as a console error or a missing tier line.
  for (const [preset, atmosphere, corona] of [
    [0, "atmosphere=O'Neil", 'corona=billboard'],
    [1, 'atmosphere=LUT 8 steps', 'corona=16 slices'],
    [2, 'atmosphere=LUT 16 steps', 'corona=32 slices'],
  ] as const) {
    await page.evaluate((value) => window.setQualityPreset?.(value), preset);
    await expect
      .poll(
        () =>
          consoleLogs.some(
            (line) => line.includes('[Quality] Active tier') && line.includes(atmosphere) && line.includes(corona),
          ),
        { timeout: 10_000 },
      )
      .toBe(true);
  }
  expect(consoleLogs.filter((line) => line.includes('[FloatLut]'))).toEqual([]);

  const fatalConsoleErrors = consoleErrors.filter(
    (line) => !allowedConsoleErrorPatterns.some((pattern) => pattern.test(line)),
  );
  expect(fatalConsoleErrors).toEqual([]);
});

test('Observe mode puts the camera on Earth, ignores movement, and restores Explore', async ({ page, baseURL }) => {
  const consoleErrors: string[] = [];
  page.on('pageerror', (error) => consoleErrors.push(error.message));

  // 2017-08-21 mid-totality from Casper, WY, opened straight from a shared link.
  const jd = 2457986.5 + (17 + 43 / 60 + 45 / 3600) / 24; // mid-totality at Casper
  const target = `${baseURL ?? '/'}?mode=observe&lat=42.8666&lon=-106.3131&jd=${jd}&az=150&el=50&fov=20`;
  const response = await page.goto(target, { waitUntil: 'domcontentloaded' });
  expect(response?.ok()).toBeTruthy();

  await page.waitForFunction(() => window.getObserveState?.()?.active === true, undefined, {
    timeout: 60_000,
  });

  const state = await page.evaluate(() => window.getObserveState?.());
  expect(state?.active).toBe(true);
  expect(state?.latDeg).toBeCloseTo(42.8666, 3);
  expect(state?.lonDeg).toBeCloseTo(-106.3131, 3);
  expect(state?.julianDate).toBeCloseTo(jd, 3);
  expect(state?.paused).toBe(true); // a link that pins the epoch holds still
  expect(state?.fovDeg).toBeCloseTo(20, 0);
  // The Moon's libration (sub-Earth point) stays within the real ~8 deg / ~7 deg limits, and the
  // chip's horizon suffix is one of the known states.
  expect(Math.abs(state?.moonLibLonDeg ?? 99)).toBeLessThan(10);
  expect(Math.abs(state?.moonLibLatDeg ?? 99)).toBeLessThan(8.5);
  expect(['above', 'below', 'unknown', 'none']).toContain(state?.eventVisibility);
  // The Moon covers the whole Sun at that instant: totality.
  expect(state?.sunCoverage).toBeGreaterThan(0.999);
  // The Sun is high over Wyoming on that date.
  expect(state?.sunAltDeg).toBeGreaterThan(45);
  expect(state?.sunAltDeg).toBeLessThan(60);

  // Stars load lazily on the first Observe frame; the quality tier decides how many are drawn
  // (500 on Low, 2000 on Medium, up to the 5000-star catalog on Full).
  await page.waitForFunction(() => (window.getObserveState?.()?.starCount ?? 0) > 0, undefined, {
    timeout: 20_000,
  });
  const starCount = (await page.evaluate(() => window.getObserveState?.()?.starCount)) ?? 0;
  expect(starCount).toBeGreaterThanOrEqual(500);
  expect(starCount).toBeLessThanOrEqual(5000);

  await expect(page.locator('#mode-observe')).toHaveAttribute('aria-pressed', 'true');
  await expect(page.locator('#observe-controls')).toBeVisible();
  await expect(page.locator('.observe-disclaimer')).toContainText('not an almanac');
  // The conjunction/sky-event chip stays available in Observe.
  await expect(page.locator('#next-sky-event')).not.toHaveAttribute('hidden');

  // WASD / the touch joystick must not move the camera, and a pose set from JS is ignored.
  const before = await page.evaluate(() => ({ ...(window.getObserveState?.() ?? {}) }));
  await page.locator('#canvas').hover();
  await page.keyboard.down('w');
  await page.waitForTimeout(500);
  await page.keyboard.up('w');
  await page.evaluate(() => window.setCameraPose?.(9999, 9999, 9999, 0, 0));
  const after = await page.evaluate(() => ({ ...(window.getObserveState?.() ?? {}) }));
  expect(after.azDeg).toBeCloseTo(before.azDeg ?? 0, 1);
  expect(after.elDeg).toBeCloseTo(before.elDeg ?? 0, 1);

  // The date control moves the sky: a day later puts the Sun somewhere else at the same clock time.
  const jdBefore = state?.julianDate ?? 0;
  await page.evaluate(() => document.querySelector<HTMLButtonElement>('[data-step-days="1"]')?.click());
  await page.waitForFunction(
    (previous) => (window.getObserveState?.()?.julianDate ?? 0) > previous + 0.5,
    jdBefore,
    { timeout: 5_000 },
  );

  // Back to Explore without reloading the module.
  await page.locator('#mode-explore').click();
  await page.waitForFunction(() => window.getObserveState?.()?.active === false, undefined, {
    timeout: 5_000,
  });
  await expect(page.locator('#observe-controls')).toBeHidden();
  expect(consoleErrors).toEqual([]);
});
