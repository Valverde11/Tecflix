import { useSyncExternalStore } from 'react';
import { useServices } from '../../hooks/useServices';
import type { User } from '../../types/user';

export interface Auth {
  readonly user: User | null;
  readonly token: string | null;
  readonly logout: () => void;
}

export function useAuth(): Auth {
  const { sessionStore } = useServices();
  const session = useSyncExternalStore(sessionStore.subscribe, sessionStore.getSession);
  return {
    user: session?.user ?? null,
    token: session?.token ?? null,
    logout: sessionStore.clear,
  };
}
