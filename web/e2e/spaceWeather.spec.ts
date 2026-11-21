import { expect, test, type Page } from '@playwright/test';

// Space weather's page side (docs/ARCHITECTURE.md §13): the controls and deep links reach the
// runtime, and the storm card and chip show C++'s numbers. Two boots, so a longer budget.
test.setTimeout(120_000);

const allowedConsoleErrorPatterns = [
    /Failed to initialize planet explorer/i,
    /Failed to load resource\/textures_low\//i,
    /not a DDS file/i,
    /\[Audio\]/i,
    /\[Texture\] Warning/i,
];

async function boot(page: Page, url: string): Promise<void> {
    await page.goto(url, { waitUntil: 'domcontentloaded' });
    await page.waitForFunction(
        () => document.getElementById('settings-status')?.textContent === 'Controls ready',
        undefined,
        { timeout: 60_000 },
    );
}

test('space weather controls, storm chip and deep links drive the runtime', async ({ page, baseURL }) => {
    const consoleErrors: string[] = [];
    page.on('console', (message) => {
        if (message.type() === 'error' && !allowedConsoleErrorPatterns.some((p) => p.test(message.text()))) {
            consoleErrors.push(message.text());
        }
    });
    page.on('pageerror', (error) => consoleErrors.push(error.message));
    const base = baseURL ?? '/';

    // Kp held at 7: Earth is in a storm, and the card says the slider holds it (oval 66° − 2°/Kp).
    await boot(page, `${base}?kp=7&wind=1`);
    await expect(page.locator('#kp-override-value')).toHaveText('Kp 7.0');
    await expect(page.locator('#solar-wind-enabled')).toBeChecked();
    await expect(page.locator('#storm-card')).toBeVisible({ timeout: 15_000 });
    await expect(page.locator('#storm-card-title')).toHaveText('Geomagnetic storm');
    await expect(page.locator('#storm-card-lines')).toContainText('held by the Kp slider');
    await expect(page.locator('#storm-card-lines')).toContainText('52° magnetic latitude');

    // The settings panel scrolls, so the group's last control is reachable on a 720 px screen.
    await page.locator('#launch-cme').click();
    await expect(page.locator('#settings-status')).toHaveText('CME launched toward Earth');

    // Whatever the date, the chip offers a catalogued storm, and playing it moves the date.
    await expect(page.locator('#next-storm')).toBeVisible();
    await expect(page.locator('#next-storm-text')).toHaveText(/^(Next geomagnetic storm|Latest catalogued storm): /);
    await page.locator('#next-storm-play').click();
    await expect(page.locator('#settings-status')).toHaveText(/^Playing /);

    await page.locator('#kp-override').fill('-1');
    await expect(page.locator('#kp-override-value')).toHaveText('Auto');
    await page.locator('#solar-wind-enabled').uncheck();
    await expect(page.locator('#settings-status')).toHaveText('Solar wind off');

    // The ticket's link: the Halloween 2003 shock reaches Earth, and the wind comes on with it.
    await boot(page, `${base}?cme=2003-10-29T06:00Z`);
    await expect(page.locator('#simulation-date')).toHaveValue('2003-10-29');
    await expect(page.locator('#solar-wind-enabled')).toBeChecked();
    await expect(page.locator('#storm-card')).toBeVisible({ timeout: 15_000 });
    await expect(page.locator('#storm-card-lines')).toContainText('Halloween storms (I)');

    expect(consoleErrors).toEqual([]);
});
