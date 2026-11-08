import type { Session } from '../types/user';
import { parseSession } from './UsersApi';

const STORAGE_KEY = 'tecflix.session';

export type SessionStorage = Pick<Storage, 'getItem' | 'setItem' | 'removeItem'>;

type Listener = () => void;

function restore(storage: SessionStorage): Session | null {
  const saved = storage.getItem(STORAGE_KEY);
  if (saved === null) {
    return null;
  }
  try {
    return parseSession(JSON.parse(saved));
  } catch {
    // Dato viejo o alterado: se descarta y la persona vuelve a iniciar sesión.
    storage.removeItem(STORAGE_KEY);
    return null;
  }
}

/**
 * Única fuente de verdad de la sesión. Vive fuera de React porque `HttpClient` también debe
 * leerla y limpiarla. Los métodos públicos son funciones flecha para poder pasarlos sueltos a
 * `useSyncExternalStore`.
 */
export class SessionStore {
  private readonly storage: SessionStorage;
  private readonly listeners = new Set<Listener>();
  private session: Session | null;

  constructor(storage: SessionStorage) {
    this.storage = storage;
    this.session = restore(storage);
  }

  getSession = (): Session | null => this.session;

  setSession = (session: Session): void => {
    this.storage.setItem(STORAGE_KEY, JSON.stringify(session));
    this.update(session);
  };

  clear = (): void => {
    this.storage.removeItem(STORAGE_KEY);
    this.update(null);
  };

  subscribe = (listener: Listener): (() => void) => {
    this.listeners.add(listener);
    return () => {
      this.listeners.delete(listener);
    };
  };

  private update(session: Session | null): void {
    this.session = session;
    this.listeners.forEach((listener) => {
      listener();
    });
  }
}
