import { StrictMode } from 'react';
import { createRoot } from 'react-dom/client';
import { loadApiConfig, type ApiConfig } from './api/apiConfig';
import { createServices } from './api/createServices';
import { App } from './App';
import './styles/global.css';

/**
 * Simula metadata mientras no esté integrada; usuarios sigue siendo la API real.
 * La comprobación de DEV deja este código y el worker fuera del build de producción.
 */
async function startMetadataMock(config: ApiConfig): Promise<void> {
  if (import.meta.env.DEV && import.meta.env.VITE_MOCK_METADATA === 'true') {
    const { network } = await import('virtual:msw');
    const { createMetadataHandlers } = await import('./test/handlers');
    network.configure({
      handlers: createMetadataHandlers(config.metadataApiUrl),
      onUnhandledFrame: 'bypass',
    });
    await network.enable();
  }
}

const rootElement = document.getElementById('root');
if (rootElement === null) {
  throw new Error('index.html no tiene el elemento #root');
}

const config = loadApiConfig(import.meta.env);
await startMetadataMock(config);
const services = createServices(config, window.sessionStorage);

createRoot(rootElement).render(
  <StrictMode>
    <App services={services} />
  </StrictMode>,
);
