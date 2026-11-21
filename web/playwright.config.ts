import { defineConfig } from '@playwright/test';

// Two runs of the same smoke suite against the same build (ARCHITECTURE §7.7):
//  - `threaded`: the preview with COEP/COOP, so the page is cross-origin isolated and
//    bootstrap.ts loads SolarSystem-mt.wasm with the job pool on Web Workers;
//  - `single`: a preview without isolation headers (SOLARSYSTEM_NO_ISOLATION=1), so the
//    single-threaded SolarSystem.wasm runs every job inline (SOLARSYSTEM_JOBS=0 on the web).
// SMOKE_TEST_URL / SMOKE_TEST_URL_SINGLE point at servers you started yourself; otherwise
// Playwright starts both previews (or reuses ones already listening).
const threadedURL = process.env.SMOKE_TEST_URL || 'http://127.0.0.1:4173/solar-system/';
const singleURL = process.env.SMOKE_TEST_URL_SINGLE || 'http://127.0.0.1:4174/solar-system/';

export default defineConfig({
    testDir: './e2e',
    timeout: 60_000,
    retries: 0,
    // One browser at a time: both projects share a software GPU, and frame timing is measured.
    workers: 1,
    use: {
        headless: true,
        launchOptions: {
            args: [
                '--no-sandbox',
                '--disable-setuid-sandbox',
                '--disable-background-timer-throttling',
                '--disable-renderer-backgrounding',
                '--enable-webgl',
                '--ignore-gpu-blocklist',
                '--use-gl=angle',
                '--use-angle=swiftshader',
                '--enable-unsafe-swiftshader',
            ],
        },
    },
    projects: [
        { name: 'threaded', use: { baseURL: threadedURL } },
        { name: 'single', use: { baseURL: singleURL } },
    ],
    webServer: [
        {
            command: 'npm run preview -- --host 127.0.0.1 --port 4173 --strictPort',
            url: 'http://127.0.0.1:4173/solar-system/',
            reuseExistingServer: true,
            timeout: 30_000,
        },
        {
            command: 'npm run preview -- --host 127.0.0.1 --port 4174 --strictPort',
            url: 'http://127.0.0.1:4174/solar-system/',
            env: { SOLARSYSTEM_NO_ISOLATION: '1' },
            reuseExistingServer: true,
            timeout: 30_000,
        },
    ],
});
