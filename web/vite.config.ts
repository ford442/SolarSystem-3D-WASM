import { defineConfig } from 'vite';

export default defineConfig({
  server: {
    fs: {
      // Allow serving files from one level up to the project root
      allow: ['..']
    }
  },
  publicDir: 'public', // This is default, but explicit for clarity
  build: {
    target: 'esnext' // Support top-level await and modern features if needed
  }
});
