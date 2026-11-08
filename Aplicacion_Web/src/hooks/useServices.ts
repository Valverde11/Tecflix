import { createContext, useContext } from 'react';
import type { Services } from '../api/createServices';

export const ServicesContext = createContext<Services | null>(null);

export function useServices(): Services {
  const services = useContext(ServicesContext);
  if (services === null) {
    throw new Error('useServices debe usarse dentro de AppProviders');
  }
  return services;
}
