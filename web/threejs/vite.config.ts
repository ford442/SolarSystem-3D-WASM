import { defineConfig } from 'vite';

export default defineConfig({
  base: '/solar-system/webgpu/',
  server: {
    fs: {
      // Allow serving files from one level up to the project root
      allow: ['..']
    }
  },
  publicDir: 'public',
  build: {
    target: 'esnext'
  }
});
