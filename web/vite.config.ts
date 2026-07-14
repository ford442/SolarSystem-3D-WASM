import { defineConfig } from 'vite';

// Match production cross-origin isolation on the app origin only.
// Asset CDNs should emit CORS + CORP, not COEP/COOP (see docs/CROSS_ORIGIN_HEADERS.md).
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
