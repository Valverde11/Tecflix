import '@testing-library/jest-dom/vitest';
import { cleanup } from '@testing-library/react';
import { afterAll, afterEach, beforeAll } from 'vitest';
import { server } from './server';

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
