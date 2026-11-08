import react from '@vitejs/plugin-react';
import { msw } from 'msw/vite';
import { defineConfig } from 'vitest/config';

// El enunciado exige 90 % en la CI de V2.0; se exige desde ya para no acumular deuda (SPEC 6.4 pide 80 en V1.0).
const MIN_COVERAGE_PERCENT = 90;
// Shaka ocupa unos 870 kB, pero va en su propio fragmento y solo se descarga al abrir un video.
const MAX_CHUNK_KILOBYTES = 1000;
// Mayor que la espera de Testing Library (setup.ts), para que falle con su mensaje detallado.
const TEST_TIMEOUT_MS = 20_000;

export default defineConfig({
  plugins: [react(), msw()],
  build: {
    chunkSizeWarningLimit: MAX_CHUNK_KILOBYTES,
  },
  test: {
    environment: 'jsdom',
    testTimeout: TEST_TIMEOUT_MS,
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
