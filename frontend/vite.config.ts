import react from '@vitejs/plugin-react';
import { msw } from 'msw/vite';
import { defineConfig } from 'vitest/config';

// SPEC 6.4: meta de V1.0. Se sube a 90 cuando se trabaje la V2.0.
const MIN_COVERAGE_PERCENT = 80;

export default defineConfig({
  plugins: [react(), msw()],
  test: {
    environment: 'jsdom',
    setupFiles: ['./src/test/setup.ts'],
    coverage: {
      provider: 'v8',
      include: ['src/**/*.{ts,tsx}'],
      exclude: ['src/main.tsx', 'src/test/**', 'src/**/*.test.{ts,tsx}', 'src/**/*.d.ts'],
      thresholds: {
        lines: MIN_COVERAGE_PERCENT,
        functions: MIN_COVERAGE_PERCENT,
        branches: MIN_COVERAGE_PERCENT,
        statements: MIN_COVERAGE_PERCENT,
      },
    },
  },
});
