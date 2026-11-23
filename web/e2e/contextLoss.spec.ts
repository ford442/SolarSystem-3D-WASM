import { expect, test } from '@playwright/test';

// A lost WebGL context is rebuilt in place (ARCHITECTURE §7.8): `?losecontext=4` drops the
// context through WEBGL_lose_context every 4 s and gives it back half a second later. Each
// time, C++ must rebuild every GPU resource from the CPU state it kept, so the frame count
// keeps climbing within 2 s of the restore, nothing is downloaded a second time, the GL layer
// reports no errors, and the reload panel never appears.
test('the scene keeps drawing through repeated context losses without refetching', async ({ page, baseURL }) => {
    test.setTimeout(90_000);
    const consoleLogs: string[] = [];
    const glErrors: string[] = [];
    const pageErrors: string[] = [];
    page.on('console', (message) => {
        const text = message.text();
        consoleLogs.push(text);
        if (/\[GL\] 0x|ERROR +\[GL\]|WebGL: INVALID_|GL_INVALID_/.test(text)) {
            glErrors.push(text);
        }
    });
    page.on('pageerror', (error) => pageErrors.push(error.message));

    // Every asset request, and whether it was made after the first loss.
    let lossesSeen = 0;
    const assetRequests: { url: string; afterLoss: boolean }[] = [];
    page.on('request', (request) => {
        const url = request.url();
        if (/\/resource\/|\.(dds|ktx2|obj|bin|json)(\?|$)/.test(url)) {
            assetRequests.push({ url, afterLoss: lossesSeen > 0 });
        }
    });
    page.on('console', (message) => {
        if (message.text().includes('[WebGL] Context lost')) lossesSeen += 1;
    });

    const response = await page.goto(`${baseURL ?? '/'}?perf=1&losecontext=4&planet=earth`, {
        waitUntil: 'domcontentloaded',
    });
    expect(response?.ok()).toBeTruthy();
    const frames = async (): Promise<number> => page.evaluate(() => window.__solarSystemPerf?.latest()?.frames ?? 0);
    await page.waitForFunction(() => (window.__solarSystemPerf?.latest()?.frames ?? 0) > 0, undefined, {
        timeout: 60_000,
    });

    for (let restore = 1; restore <= 2; restore += 1) {
        await expect
            .poll(() => consoleLogs.filter((line) => line.includes('[GL] context restored')).length, {
                message: `restore #${String(restore)}`,
                timeout: 20_000,
            })
            .toBeGreaterThanOrEqual(restore);
        const atRestore = await frames();
        await expect
            .poll(frames, { message: `frames after restore #${String(restore)}`, timeout: 2_000, intervals: [100] })
            .toBeGreaterThan(atRestore);
    }

    expect(consoleLogs.some((line) => line.includes('[WebGL] Context restored after'))).toBe(true);
    await expect(page.locator('#fatal-panel')).toHaveCount(0);
    expect(glErrors).toEqual([]);
    expect(pageErrors).toEqual([]);
    // Staged loading may still be fetching systems it had not reached, but nothing the page had
    // already downloaded may be requested again: the restore reads MEMFS, not the network.
    const before = new Set(assetRequests.filter((entry) => !entry.afterLoss).map((entry) => entry.url));
    const refetched = assetRequests
        .filter((entry) => entry.afterLoss && before.has(entry.url))
        .map((entry) => entry.url);
    expect(refetched).toEqual([]);
});
