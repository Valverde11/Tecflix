import { StrictMode } from 'react';
import { createRoot } from 'react-dom/client';
import { loadApiConfig } from './api/apiConfig';
import { createServices } from './api/createServices';
import { App } from './App';
import './styles/global.css';

const rootElement = document.getElementById('root');
if (rootElement === null) {
  throw new Error('index.html no tiene el elemento #root');
}

const services = createServices(loadApiConfig(import.meta.env), window.sessionStorage);

createRoot(rootElement).render(
  <StrictMode>
    <App services={services} />
  </StrictMode>,
);
