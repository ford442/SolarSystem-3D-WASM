import { readFile } from 'node:fs/promises';
import { resolve, sep } from 'node:path';
import { fileURLToPath } from 'node:url';
import { defineConfig, type Plugin } from 'vite';

// Dev only: serve ../resource/shaders at <base>__dev/shaders/ for the shader hot reload
// (src/shaderReload.ts). The build preloads shaders into the .data bundle instead.
const shaderRoot = fileURLToPath(new URL('../resource/shaders', import.meta.url));
function devShaderRoute(): Plugin {
  return {
    name: 'solar-system-dev-shaders',
    apply: 'serve',
    configureServer(server) {
      server.middlewares.use(`${server.config.base}__dev/shaders`, (req, res, next) => {
        const relative = decodeURIComponent((req.url ?? '').split('?')[0]).replace(/^\/+/, '');
        const file = resolve(shaderRoot, relative);
        if (!file.startsWith(shaderRoot + sep)) {
          res.statusCode = 403;
          res.end();
          return;
        }
        readFile(file, 'utf8').then((text) => {
          res.setHeader('Content-Type', 'text/plain; charset=utf-8');
          res.setHeader('Cache-Control', 'no-store');
          res.end(text);
        }, () => next());
      });
    },
  };
}

// Dev/preview serve Option B (COEP+COOP), which production uses too now that the pthread
// build ships: on an isolated page bootstrap.ts loads SolarSystem-mt.js and the job pool runs
// on workers (docs/CROSS_ORIGIN_HEADERS.md). Asset CDNs emit CORS + CORP, not COEP/COOP.
// SOLARSYSTEM_NO_ISOLATION=1 serves Option A instead (no isolation: the single-threaded
// build), which the second Playwright smoke run uses.
const isolationOff = process.env.SOLARSYSTEM_NO_ISOLATION === '1';
const appIsolationHeaders: Record<string, string> = isolationOff
  ? {}
  : {
      'Cross-Origin-Embedder-Policy': 'require-corp',
      'Cross-Origin-Opener-Policy': 'same-origin',
      'Cross-Origin-Resource-Policy': 'same-origin',
    };

export default defineConfig({
  base: '/solar-system/',
  plugins: [devShaderRoute()],
  server: {
    fs: {
      // Allow serving files from one level up to the project root
      allow: ['..']
    },
    headers: appIsolationHeaders,
  },
  preview: {
    headers: appIsolationHeaders,
  },
  publicDir: 'public', // This is default, but explicit for clarity
  build: {
    target: 'esnext' // Support top-level await and modern features if needed
  }
});
