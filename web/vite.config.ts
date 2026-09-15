import { defineConfig } from 'vite';

// Dev/preview rehearse Option B (COEP+COOP) for a future pthread / SharedArrayBuffer
// build. Production stays Option A (no COEP) until pthreads ship — see
// docs/CROSS_ORIGIN_HEADERS.md. Asset CDNs should emit CORS + CORP, not COEP/COOP.
const appIsolationHeaders = {
  'Cross-Origin-Embedder-Policy': 'require-corp',
  'Cross-Origin-Opener-Policy': 'same-origin',
  'Cross-Origin-Resource-Policy': 'same-origin',
};

export default defineConfig({
  base: '/solar-system/',
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
