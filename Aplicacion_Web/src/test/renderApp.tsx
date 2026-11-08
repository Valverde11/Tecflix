import { render } from '@testing-library/react';
import { MemoryRouter } from 'react-router';
import { createServices, type Services } from '../api/createServices';
import { AppProviders } from '../AppProviders';
import { AppRoutes } from '../AppRoutes';
import type { Session } from '../types/user';
import { TEST_API_CONFIG } from './fixtures';

/** Monta la aplicación completa en una ruta, con servicios nuevos para no compartir estado. */
export function renderApp(path: string, session: Session | null = null): Services {
  window.sessionStorage.clear();
  const services = createServices(TEST_API_CONFIG, window.sessionStorage);
  if (session !== null) {
    services.sessionStore.setSession(session);
  }

  render(
    <MemoryRouter initialEntries={[path]}>
      <AppProviders services={services}>
        <AppRoutes />
      </AppProviders>
    </MemoryRouter>,
  );
  return services;
}
