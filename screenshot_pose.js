import puppeteer from 'puppeteer';
import fs from 'fs';
import path from 'path';

const URL = process.argv[2] || 'https://test.1ink.us/solar-system/index.html';
const OUT = process.argv[3] || '/content/SolarSystem-3D-WASM/headless_chrome/screenshot_pose.png';

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
    console.error(entry);
  });

  console.log(`Navigating to ${URL} ...`);
  await page.goto(URL, { waitUntil: 'load', timeout: 120000 });

  // Wait for WASM to initialize and core assets to load.
  console.log('Waiting 20s for core loading...');
  await new Promise(r => setTimeout(r, 20000));

  // Teleport camera near Earth so the staged-loading system initializes Earth.
  const hasPose = await page.evaluate(() => typeof window.setCameraPose === 'function');
  if (hasPose) {
    console.log('Teleporting camera near Earth...');
    await page.evaluate(() => {
      window.setCameraPose(1850, 0, 0, 0, 0);
    });
  } else {
    console.log('window.setCameraPose not available');
  }

  // Wait for Earth assets to download and the system to initialize.
  console.log('Waiting 15s for staged Earth system init...');
  await new Promise(r => setTimeout(r, 15000));

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
