import { randomBytes } from 'node:crypto';
import { mkdtempSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { join } from 'node:path';
import { defineConfig, devices } from '@playwright/test';

/**
 * Flujo de punta a punta (W6.2). La web corre con `vite dev`; por defecto metadata y streaming
 * se simulan con MSW (E2E_MOCK_VIDEO_APIS=false usa las reales). La API de usuarios siempre es
 * la real: se reutiliza la que ya corra en USERS_API_URL o, si se da E2E_USERS_SERVICE_COMMAND,
 * Playwright la arranca con datos y secretos nuevos.
 */
const WEB_PORT = 5173;
const WEB_URL = `http://localhost:${String(WEB_PORT)}`;
const USERS_API_URL = process.env.E2E_USERS_API_URL ?? 'http://localhost:8081';
const METADATA_API_URL = process.env.E2E_METADATA_API_URL ?? 'http://localhost:8082';
const STREAMING_API_URL = process.env.E2E_STREAMING_API_URL ?? 'http://localhost:8083';
const MOCK_VIDEO_APIS = process.env.E2E_MOCK_VIDEO_APIS ?? 'true';
const USERS_SERVICE_COMMAND = process.env.E2E_USERS_SERVICE_COMMAND;
const IS_CI = process.env.CI !== undefined;
// La API de usuarios exige secretos de al menos 32 bytes; 32 bytes en hex son 64 caracteres.
const SECRET_BYTES = 32;
const SERVER_START_TIMEOUT_MS = 120_000;
// Registrar y entrar calculan PBKDF2 con 600 000 iteraciones: en Debug el flujo pasa de 1 min.
const TEST_TIMEOUT_MS = 180_000;

function newSecret(): string {
  return randomBytes(SECRET_BYTES).toString('hex');
}

const webServer = {
  command: `npm run dev -- --port ${String(WEB_PORT)} --strictPort`,
  url: WEB_URL,
  reuseExistingServer: !IS_CI,
  timeout: SERVER_START_TIMEOUT_MS,
  // Las variables del proceso mandan sobre .env: la prueba no depende de la configuración local.
  env: {
    VITE_USERS_API_URL: USERS_API_URL,
    VITE_METADATA_API_URL: METADATA_API_URL,
    VITE_STREAMING_API_URL: STREAMING_API_URL,
    VITE_MOCK_VIDEO_APIS: MOCK_VIDEO_APIS,
  },
};

const usersServer =
  USERS_SERVICE_COMMAND === undefined
    ? []
    : [
        {
          command: USERS_SERVICE_COMMAND,
          url: `${USERS_API_URL}/health`,
          reuseExistingServer: !IS_CI,
          timeout: SERVER_START_TIMEOUT_MS,
          env: {
            PORT: new URL(USERS_API_URL).port,
            // Carpeta nueva en cada corrida: la prueba no ensucia ni depende de datos anteriores.
            DATA_DIR: mkdtempSync(join(tmpdir(), 'tecflix-e2e-')),
            AUTH_SECRET: newSecret(),
            SERVICE_SECRET: newSecret(),
            CORS_ORIGIN: WEB_URL,
          },
        },
      ];

export default defineConfig({
  testDir: './e2e',
  timeout: TEST_TIMEOUT_MS,
  forbidOnly: IS_CI,
  retries: IS_CI ? 1 : 0,
  reporter: IS_CI ? [['list'], ['html', { open: 'never' }]] : 'list',
  use: {
    baseURL: WEB_URL,
    trace: 'retain-on-failure',
  },
  projects: [{ name: 'chromium', use: { ...devices['Desktop Chrome'] } }],
  webServer: [...usersServer, webServer],
});
