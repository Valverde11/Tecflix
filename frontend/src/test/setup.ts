import '@testing-library/jest-dom/vitest';
import { cleanup, configure } from '@testing-library/react';
import { afterAll, afterEach, beforeAll } from 'vitest';
import { server } from './server';

// Con todos los archivos en paralelo, un flujo que escribe en varios campos supera 1 s.
const ASYNC_TIMEOUT_MS = 5000;
configure({ asyncUtilTimeout: ASYNC_TIMEOUT_MS });

// 'error': una ruta sin simular debe fallar, no salir a la red ni pasar en silencio.
beforeAll(() => {
  server.listen({ onUnhandledFrame: 'error' });
});

// Sin los globales de Vitest, Testing Library no limpia solo el DOM entre pruebas.
afterEach(() => {
  cleanup();
  server.resetHandlers();
});

afterAll(() => {
  server.close();
});
