import { BrowserRouter } from 'react-router';
import type { Services } from './api/createServices';
import { AppProviders } from './AppProviders';
import { AppRoutes } from './AppRoutes';

interface AppProps {
  readonly services: Services;
}

export function App({ services }: AppProps) {
  return (
    <BrowserRouter>
      <AppProviders services={services}>
        <AppRoutes />
      </AppProviders>
    </BrowserRouter>
  );
}
