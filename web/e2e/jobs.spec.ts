import { expect, test } from '@playwright/test';

// The job pool on both builds (playwright.config.ts runs every spec twice): on the isolated
// `threaded` server the page loads SolarSystem-mt.wasm and C++ starts worker threads; on the
// `single` server (no COEP) the single-threaded build runs every job inline. Either way the
// perf HUD reports it, textures stream through the upload budget, and field lines (traced in
// jobs) appear.
test('Jobs run on workers when isolated and inline otherwise; ?perf=1 reports them', async ({
    page,
    baseURL,
}, testInfo) => {
    const threaded = testInfo.project.name === 'threaded';
    const consoleLogs: string[] = [];
    const pageErrors: string[] = [];
    page.on('console', (message) => consoleLogs.push(message.text()));
    page.on('pageerror', (error) => pageErrors.push(error.message));

    const response = await page.goto(`${baseURL ?? '/'}?perf=1&quality=medium`, {
        waitUntil: 'domcontentloaded',
    });
    expect(response?.ok()).toBeTruthy();
    expect(await page.evaluate(() => window.crossOriginIsolated)).toBe(threaded);

    await page.waitForFunction(() => (window.__solarSystemPerf?.latest()?.frames ?? 0) > 0, undefined, {
        timeout: 60_000,
    });
    const jobsLine = consoleLogs.find((line) => line.includes('[Jobs]') && line.includes('worker thread'));
    expect(jobsLine, 'the [Jobs] start-up line').toBeDefined();
    const workers = Number(/\[Jobs\] (\d+) worker/.exec(jobsLine ?? '')?.[1] ?? -1);
    if (threaded) {
        expect(consoleLogs.some((line) => line.includes('loading the threaded build'))).toBe(true);
        expect(workers).toBeGreaterThanOrEqual(1);
    } else {
        expect(workers).toBe(0);
    }
    await expect(page.locator('#perf-hud')).toBeVisible();
    await expect(page.locator('#perf-hud')).toContainText('stage');
    await expect(page.locator('#perf-hud')).toContainText(threaded ? 'workers' : 'jobs inline');
    expect((await page.evaluate(() => window.__solarSystemPerf?.latest()?.jobs.workers)) ?? -1).toBe(workers);

    // Magnetic field lines are traced in jobs and appear once every trace is in.
    await page.evaluate(() => {
        const toggle = document.getElementById('magnetic-fields-enabled') as HTMLInputElement | null;
        if (toggle && !toggle.checked) {
            toggle.click();
        }
    });
    await expect
        .poll(() => consoleLogs.some((line) => line.includes('[MagneticField] Built field-line ribbons')), {
            timeout: 20_000,
        })
        .toBe(true);

    // A staged system's textures arrive through the job decode and the upload pump.
    await expect
        .poll(() => consoleLogs.some((line) => line.includes('[StagedLoading] Required assets ready for')), {
            timeout: 30_000,
        })
        .toBe(true);

    // Frame timing on this machine, for the record: the 33 ms acceptance is for a real
    // medium-tier device, not the CI software rasteriser, so it is reported and not asserted.
    await page.waitForTimeout(5_000);
    const stats = await page.evaluate(() => window.__solarSystemPerf?.latest() ?? null);
    expect(stats).not.toBeNull();
    testInfo.annotations.push({
        type: 'frame-timing',
        description: `${testInfo.project.name}: ${String(stats?.slowFrames)} of ${String(stats?.frames)} frames over 33 ms; last ${String(stats?.frameMs.toFixed(1))} ms; GPU timers ${stats?.gpu ? 'on' : stats?.gpuSupported ? 'pending' : 'unavailable'}`,
    });
    expect(pageErrors).toEqual([]);
});
