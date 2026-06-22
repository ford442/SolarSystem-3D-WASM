import puppeteer from 'puppeteer';

const URL = 'http://localhost:4173/solar-system/';
const logs = [];

(async () => {
  const browser = await puppeteer.launch({
    headless: 'new',
    args: ['--no-sandbox', '--disable-gpu', '--enable-unsafe-swiftshader'],
    executablePath: '/usr/bin/google-chrome',
  });
  const page = await browser.newPage();
  page.on('console', msg => {
    const t = msg.text();
    logs.push(t);
    if (/Loaded|fallback|WARNING|MAX_LEVEL|incomplete|black|texture/i.test(t)) {
      console.log('[console]', t);
    }
  });
  page.on('pageerror', e => console.error('PAGEERR', e.message));
  await page.goto(URL, { waitUntil: 'networkidle0', timeout: 15000 }).catch(e=>console.log('goto err',e.message));
  // Give WASM a bit to init and start texture fetches + prints
  await new Promise(r => setTimeout(r, 6000));
  await browser.close();

  const relevant = logs.filter(l => /Loaded|fallback|WARNING.*texture|MAX_LEVEL|incomplete|WebGL|error/i.test(l));
  console.log('=== FILTERED RELEVANT LOGS ===');
  console.log(relevant.join('\n') || '(no matching logs captured)');
  console.log('=== TOTAL CONSOLE MESSAGES:', logs.length, ' ===');
  // Look for success indicators
  const hasLoad = logs.some(l => /Loaded$|Loaded\s*$/.test(l) || /Created 4x4 fallback/.test(l));
  const hasBlackWarn = logs.some(l => /black|incomplete.*texture/i.test(l));
  console.log('SUCCESS_INDICATOR (saw Loaded or fallback):', hasLoad);
  console.log('NO_BLACK_WARN:', !hasBlackWarn);
  process.exit(0);
})().catch(e => { console.error(e); process.exit(1); });