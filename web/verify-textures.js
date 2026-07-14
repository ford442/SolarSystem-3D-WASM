import puppeteer from 'puppeteer';
import { existsSync } from 'node:fs';

const url = process.env.VERIFY_TEXTURES_URL || 'http://localhost:4173/solar-system/';
const timeoutMs = Number(process.env.VERIFY_TEXTURES_TIMEOUT_MS || 30000);
const settleMs = Number(process.env.VERIFY_TEXTURES_SETTLE_MS || 12000);
const logs = [];
const pageErrors = [];
const skyboxFaces = [
  'PositiveX.dds',
  'NegativeX.dds',
  'PositiveY.dds',
  'NegativeY.dds',
  'PositiveZ.dds',
  'NegativeZ.dds',
];

const fatalTexturePatterns = [
  /incomplete[-\s]+texture/i,
  /black[-\s]+texture/i,
  /texture[^\n]*(?:incomplete|black|not renderable)/i,
  /gpu upload incomplete/i,
  /GL_TEXTURE_MAX_LEVEL\s*=\s*0/i,
];

function hasSkyboxFace(face) {
  const expected = `resource/textures_low/Main_SkyBox/${face} Loaded`;
  return logs.some(({ text }) => text.includes(expected));
}

async function waitForSkybox() {
  const deadline = Date.now() + settleMs;
  while (Date.now() < deadline) {
    if (skyboxFaces.every(hasSkyboxFace)) return;
    await new Promise(resolve => setTimeout(resolve, 250));
  }
}

let browser;
try {
  const executablePath = process.env.PUPPETEER_EXECUTABLE_PATH ||
    ['/usr/bin/google-chrome-stable', '/usr/bin/google-chrome'].find(existsSync);
  const launchOptions = {
    headless: true,
    args: [
      '--no-sandbox',
      '--disable-setuid-sandbox',
      '--disable-breakpad',
      '--disable-crash-reporter',
      '--enable-webgl',
      '--ignore-gpu-blocklist',
      '--use-gl=angle',
      '--use-angle=swiftshader',
      '--enable-unsafe-swiftshader',
    ],
  };
  if (executablePath) {
    launchOptions.executablePath = executablePath;
  }

  browser = await puppeteer.launch(launchOptions);
  const page = await browser.newPage();
  page.on('console', message => {
    const entry = { type: message.type(), text: message.text() };
    logs.push(entry);
    if (/Loaded|fallback|WARNING|MAX_LEVEL|incomplete|black|texture|WebGL/i.test(entry.text)) {
      console.log(`[console:${entry.type}]`, entry.text);
    }
  });
  page.on('pageerror', error => {
    pageErrors.push(error.message);
    console.error('PAGEERR', error.message);
  });

  const response = await page.goto(url, { waitUntil: 'domcontentloaded', timeout: timeoutMs });
  if (!response || !response.ok()) {
    throw new Error(`Preview navigation failed: ${response?.status() ?? 'no response'} ${url}`);
  }

  await waitForSkybox();

  const relevant = logs.filter(({ text }) =>
    /Loaded|fallback|WARNING.*texture|MAX_LEVEL|incomplete|black|WebGL|error/i.test(text)
  );
  console.log('=== FILTERED RELEVANT LOGS ===');
  console.log(relevant.map(({ type, text }) => `[${type}] ${text}`).join('\n') || '(no matching logs captured)');
  console.log(`=== TOTAL CONSOLE MESSAGES: ${logs.length} ===`);

  const missingSkyboxFaces = skyboxFaces.filter(face => !hasSkyboxFace(face));
  const fatalTextureLogs = logs.filter(({ text }) =>
    fatalTexturePatterns.some(pattern => pattern.test(text))
  );

  console.log('SKYBOX_FACES_LOADED:', `${skyboxFaces.length - missingSkyboxFaces.length}/${skyboxFaces.length}`);
  console.log('NO_INCOMPLETE_OR_BLACK_TEXTURE_WARNINGS:', fatalTextureLogs.length === 0);

  const failures = [];
  if (missingSkyboxFaces.length > 0) {
    failures.push(`Missing skybox load confirmations: ${missingSkyboxFaces.join(', ')}`);
  }
  if (fatalTextureLogs.length > 0) {
    failures.push(
      `Incomplete/black texture diagnostics:\n${fatalTextureLogs.map(({ text }) => `  ${text}`).join('\n')}`
    );
  }
  if (pageErrors.length > 0) {
    failures.push(`Unhandled page errors:\n${pageErrors.map(error => `  ${error}`).join('\n')}`);
  }

  if (failures.length > 0) {
    throw new Error(`Texture verification failed:\n${failures.join('\n')}`);
  }

  console.log('Texture verification passed.');
} catch (error) {
  console.error(error);
  process.exitCode = 1;
} finally {
  if (browser) await browser.close();
}
