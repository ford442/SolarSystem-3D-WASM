import puppeteer from 'puppeteer';
import fs from 'fs';
import path from 'path';

const URL = process.argv[2] || 'https://test.1ink.us/solar-system/index.html';
const OUT = process.argv[3] || '/content/SolarSystem-3D-WASM/headless_chrome/screenshot_intercept.png';

const chromeArgs = [
  '--no-sandbox',
  '--headless=new',
  '--enable-unsafe-swiftshader',
  '--ignore-gpu-blocklist',
  '--enable-gpu-rasterization',
  '--enable-zero-copy',
  '--disable-search-engine-choice-screen',
  '--ash-no-nudges',
  '--no-first-run',
  '--disable-features=Translate',
  '--no-default-browser-check',
  '--window-size=1280,720',
  '--hide-scrollbars',
];

async function run() {
  const browser = await puppeteer.launch({
    headless: 'new',
    ignoreDefaultArgs: true,
    executablePath: process.env.PUPPETEER_EXECUTABLE_PATH || '/usr/bin/google-chrome',
    args: chromeArgs,
  });

  const page = await browser.newPage();
  await page.setViewport({ width: 1280, height: 720 });
  await page.setRequestInterception(true);

  const blockedUrls = [];
  page.on('request', req => {
    const url = req.url();
    // Block very large skybox and star textures to avoid SwiftShader OOM.
    if (url.includes('Main_SkyBox') || url.includes('Star_Spectrum') || url.includes('flares_bright')) {
      blockedUrls.push(url);
      req.abort('aborted');
      return;
    }
    req.continue();
  });

  const logs = [];
  page.on('console', msg => {
    const text = msg.text();
    logs.push(`[console] ${text}`);
    console.log(text);
  });
  page.on('pageerror', err => {
    const stack = err.stack || '';
    logs.push(`[pageerror] ${err.message}\n${stack}`);
    console.error('Page error:', err.message, stack);
  });
  page.on('requestfailed', req => {
    const entry = `[requestfailed] ${req.url()}: ${req.failure()?.errorText}`;
    logs.push(entry);
  });

  console.log(`Navigating to ${URL} ...`);
  await page.goto(URL, { waitUntil: 'load', timeout: 120000 });

  // Try to move camera toward Earth (proxy at ~1900,0,0). Use keyboard/mouse or JS.
  // We'll inject a camera teleport after some initialization.
  await new Promise(r => setTimeout(r, 15000));

  console.log('Blocked URLs:', blockedUrls);

  fs.mkdirSync(path.dirname(OUT), { recursive: true });
  await page.screenshot({ path: OUT, fullPage: false });
  console.log(`Screenshot saved to ${OUT}`);

  fs.writeFileSync(OUT.replace(/\.png$/i, '.log.txt'), logs.join('\n'));

  await browser.close();
}

run().catch(err => {
  console.error('Screenshot script failed:', err);
  process.exit(1);
});
