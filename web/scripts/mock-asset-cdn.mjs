#!/usr/bin/env node
/**
 * Minimal static server that mimics a production asset CDN origin.
 * Serves web/public/ (or project resource/) with CORS + CORP only — no COEP/COOP.
 *
 * Usage:
 *   node scripts/mock-asset-cdn.mjs
 *   ASSET_CDN_PORT=5199 ASSET_CDN_ROOT=../public node scripts/mock-asset-cdn.mjs
 */
import { createServer } from 'node:http';
import { readFile } from 'node:fs/promises';
import { existsSync } from 'node:fs';
import { dirname, join, normalize } from 'node:path';
import { fileURLToPath } from 'node:url';

const __dirname = dirname(fileURLToPath(import.meta.url));
const port = Number(process.env.ASSET_CDN_PORT || 5199);
const root = normalize(
  process.env.ASSET_CDN_ROOT || join(__dirname, '..', 'public'),
);

const mime = {
  '.dds': 'application/octet-stream',
  '.wasm': 'application/wasm',
  '.js': 'text/javascript',
  '.json': 'application/json',
  '.mp3': 'audio/mpeg',
  '.html': 'text/html',
};

function send(res, status, body, headers = {}) {
  res.writeHead(status, {
    'Access-Control-Allow-Origin': '*',
    'Access-Control-Allow-Methods': 'GET, HEAD, OPTIONS',
    'Access-Control-Expose-Headers': 'Content-Length',
    'Cross-Origin-Resource-Policy': 'cross-origin',
    ...headers,
  });
  res.end(body);
}

const server = createServer(async (req, res) => {
  if (!req.url) {
    send(res, 400, 'Bad request');
    return;
  }

  if (req.method === 'OPTIONS') {
    send(res, 204, '');
    return;
  }

  if (req.method !== 'GET' && req.method !== 'HEAD') {
    send(res, 405, 'Method not allowed');
    return;
  }

  const urlPath = decodeURIComponent(new URL(req.url, `http://127.0.0.1:${port}`).pathname);
  const relative = urlPath.replace(/^\/+/, '');
  const filePath = normalize(join(root, relative));

  if (!filePath.startsWith(root) || !existsSync(filePath)) {
    send(res, 404, 'Not found', { 'Content-Type': 'text/plain' });
    return;
  }

  const ext = filePath.slice(filePath.lastIndexOf('.'));
  const data = await readFile(filePath);
  const headers = {
    'Content-Type': mime[ext] || 'application/octet-stream',
    'Cache-Control': 'public, max-age=60',
  };

  if (req.method === 'HEAD') {
    send(res, 200, '', { ...headers, 'Content-Length': data.length });
    return;
  }

  send(res, 200, data, { ...headers, 'Content-Length': data.length });
});

server.listen(port, '127.0.0.1', () => {
  console.log(`Mock asset CDN: http://127.0.0.1:${port}/ (root: ${root})`);
  console.log('Headers: Access-Control-Allow-Origin: *, Cross-Origin-Resource-Policy: cross-origin');
  console.log('No COEP/COOP on asset responses (recommended production pattern).');
});
