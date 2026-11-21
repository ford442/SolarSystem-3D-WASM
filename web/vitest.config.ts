import { defineConfig } from 'vitest/config';

// Unit tests only (pure TypeScript, Node environment). The Playwright smoke test in e2e/
// drives the real app and runs with `npm run test:smoke`.
export default defineConfig({
  test: {
    include: ['src/**/*.test.ts'],
    environment: 'node',
  },
});
