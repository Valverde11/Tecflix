import { useSyncExternalStore } from 'react';
import { useServices } from '../../hooks/useServices';
import type { User } from '../../types/user';

export interface Auth {
  readonly user: User | null;
  readonly logout: () => void;
}

export function useAuth(): Auth {
  const { sessionStore } = useServices();
  const session = useSyncExternalStore(sessionStore.subscribe, sessionStore.getSession);
  return { user: session?.user ?? null, logout: sessionStore.clear };
}
