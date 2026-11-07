import { StrictMode } from 'react';
import { createRoot } from 'react-dom/client';
import { loadApiConfig, type ApiConfig } from './api/apiConfig';
import { createServices } from './api/createServices';
import { App } from './App';
import './styles/global.css';

/**
 * Simula metadata y streaming mientras no estén integradas; usuarios sigue siendo la API real.
 * La comprobación de DEV deja este código y el worker fuera del build de producción.
 */
async function startVideoApisMock(config: ApiConfig): Promise<void> {
  if (import.meta.env.DEV && import.meta.env.VITE_MOCK_VIDEO_APIS === 'true') {
    const { network } = await import('virtual:msw');
    const { createMetadataHandlers, createStreamingHandlers } = await import('./test/handlers');
    network.configure({
      handlers: [
        ...createMetadataHandlers(config.metadataApiUrl),
        ...createStreamingHandlers(config.streamingApiUrl),
      ],
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
await startVideoApisMock(config);
const services = createServices(config, window.sessionStorage);

createRoot(rootElement).render(
  <StrictMode>
    <App services={services} />
  </StrictMode>,
);
