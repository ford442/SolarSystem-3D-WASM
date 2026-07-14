#!/usr/bin/env node
/**
 * Verifies that runtime asset fetches work under COEP require-corp:
 * 1) Same-origin assets (preview / dev server)
 * 2) Cross-origin assets (mock CDN on a different port)
 *
 * Exercises the same XHR path Emscripten uses for emscripten_async_wget2.
 *
 * Env:
 *   VERIFY_APP_URL          default http://127.0.0.1:4173/solar-system/
 *   VERIFY_ASSET_CDN_URL    default http://127.0.0.1:5199/
 *   VERIFY_TEXTURE_PATH     default resource/textures_low/Ariel_Diffuse_Low.dds
 */
import puppeteer from 'puppeteer';
import { existsSync } from 'node:fs';

const appUrl = process.env.VERIFY_APP_URL || 'http://127.0.0.1:4173/solar-system/';
const assetCdnUrl = (process.env.VERIFY_ASSET_CDN_URL || 'http://127.0.0.1:5199/').replace(/\/?$/, '/');
const texturePath = process.env.VERIFY_TEXTURE_PATH || 'resource/textures_low/Ariel_Diffuse_Low.dds';
const timeoutMs = Number(process.env.VERIFY_FETCH_TIMEOUT_MS || 15000);

async function xhrGet(page, url) {
  return page.evaluate(async targetUrl => {
    return new Promise((resolve, reject) => {
      const xhr = new XMLHttpRequest();
      xhr.open('GET', targetUrl, true);
      xhr.responseType = 'arraybuffer';
      xhr.onload = () => {
        resolve({
          status: xhr.status,
          byteLength: xhr.response?.byteLength ?? 0,
          responseType: xhr.responseType,
        });
      };
      xhr.onerror = () => reject(new Error(`XHR network error for ${targetUrl}`));
      xhr.onabort = () => reject(new Error(`XHR aborted for ${targetUrl}`));
      xhr.send();
    });
  }, url);
}

async function probeHeaders(url) {
  const response = await fetch(url, { method: 'HEAD' });
  const headers = {};
  for (const [key, value] of response.headers.entries()) {
    headers[key.toLowerCase()] = value;
  }
  return { ok: response.ok, status: response.status, headers };
}

function assertNoSilentFailure(label, result, minBytes = 1) {
  if (!result || result.status < 200 || result.status >= 300) {
    throw new Error(`${label}: HTTP ${result?.status ?? 'no response'}`);
  }
  if (result.byteLength < minBytes) {
    throw new Error(`${label}: empty body (${result.byteLength} bytes)`);
  }
  console.log(`OK ${label}: status=${result.status}, bytes=${result.byteLength}`);
}

let browser;
try {
  const appOrigin = new URL(appUrl).origin;
  const cdnOrigin = new URL(assetCdnUrl).origin;
  const sameOriginAssetUrl = new URL(texturePath, appUrl).toString();
  const crossOriginAssetUrl = new URL(texturePath, assetCdnUrl).toString();

  console.log('=== Header probe (mock CDN asset) ===');
  const cdnProbe = await probeHeaders(crossOriginAssetUrl);
  console.log('CDN status:', cdnProbe.status);
  console.log('CDN headers:', JSON.stringify(cdnProbe.headers, null, 2));
  if (!cdnProbe.ok) {
    throw new Error(`Mock CDN asset not reachable: ${crossOriginAssetUrl}`);
  }
  if (!cdnProbe.headers['access-control-allow-origin']) {
    throw new Error('Mock CDN missing Access-Control-Allow-Origin');
  }
  if (cdnProbe.headers['cross-origin-resource-policy'] !== 'cross-origin') {
    throw new Error('Mock CDN should set Cross-Origin-Resource-Policy: cross-origin');
  }
  if (cdnProbe.headers['cross-origin-embedder-policy']) {
    console.warn('WARN: CDN emits COEP — not recommended on asset responses');
  }

  const executablePath = process.env.PUPPETEER_EXECUTABLE_PATH ||
    ['/usr/bin/google-chrome-stable', '/usr/bin/google-chrome'].find(existsSync);
  const launchOptions = {
    headless: true,
    args: ['--no-sandbox', '--disable-setuid-sandbox'],
  };
  if (executablePath) launchOptions.executablePath = executablePath;

  browser = await puppeteer.launch(launchOptions);
  const page = await browser.newPage();

  const failedRequests = [];
  page.on('requestfailed', request => {
    failedRequests.push({ url: request.url(), reason: request.failure()?.errorText });
  });

  console.log('=== Load app document (COEP/COOP from Vite) ===');
  const nav = await page.goto(appUrl, { waitUntil: 'domcontentloaded', timeout: timeoutMs });
  if (!nav?.ok()) {
    throw new Error(`App navigation failed: ${nav?.status()} ${appUrl}`);
  }

  const isolation = await page.evaluate(() => ({
    crossOriginIsolated: window.crossOriginIsolated,
    coep: document.querySelector('meta[http-equiv="Cross-Origin-Embedder-Policy"]')?.content ?? '(response header)',
  }));
  console.log('crossOriginIsolated:', isolation.crossOriginIsolated);

  console.log('=== XHR same-origin asset (simulates wget2 same-origin) ===');
  const sameOrigin = await xhrGet(page, sameOriginAssetUrl);
  assertNoSilentFailure('same-origin XHR', sameOrigin);

  console.log('=== XHR cross-origin asset (simulates wget2 + VITE_ASSET_BASE CDN) ===');
  const crossOrigin = await xhrGet(page, crossOriginAssetUrl);
  assertNoSilentFailure('cross-origin XHR under COEP', crossOrigin);

  if (failedRequests.length > 0) {
    throw new Error(`Silent network failures:\n${failedRequests.map(f => `  ${f.url} — ${f.reason}`).join('\n')}`);
  }

  console.log('=== Summary ===');
  console.log(`App origin:  ${appOrigin}`);
  console.log(`CDN origin:  ${cdnOrigin}`);
  console.log('Cross-origin fetch verification passed (XHR path matches emscripten_async_wget2).');
} catch (error) {
  console.error(error);
  process.exitCode = 1;
} finally {
  if (browser) await browser.close();
}
