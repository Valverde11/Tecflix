import { QueryClientProvider } from '@tanstack/react-query';
import type { ReactNode } from 'react';
import type { Services } from './api/createServices';
import { ServicesContext } from './hooks/useServices';

interface AppProvidersProps {
  readonly services: Services;
  readonly children: ReactNode;
}

export function AppProviders({ services, children }: AppProvidersProps) {
  return (
    <ServicesContext value={services}>
      <QueryClientProvider client={services.queryClient}>{children}</QueryClientProvider>
    </ServicesContext>
  );
}
