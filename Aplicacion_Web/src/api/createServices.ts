import { QueryClient } from '@tanstack/react-query';
import type { ApiConfig } from './apiConfig';
import { HttpClient, type AuthHandler } from './HttpClient';
import { MetadataApi } from './MetadataApi';
import { SessionStore, type SessionStorage } from './SessionStore';
import { StreamingApi } from './StreamingApi';
import { UsersApi } from './UsersApi';

export interface Services {
  readonly sessionStore: SessionStore;
  readonly queryClient: QueryClient;
  readonly usersApi: UsersApi;
  readonly metadataApi: MetadataApi;
  readonly streamingApi: StreamingApi;
}

/** Arma todos los objetos una sola vez; así no hay estado global y cada prueba crea el suyo. */
export function createServices(config: ApiConfig, storage: SessionStorage): Services {
  const sessionStore = new SessionStore(storage);
  // Sin reintentos: un 4xx no cambia al repetirlo y la pantalla ofrece reintentar a la persona.
  const queryClient = new QueryClient({ defaultOptions: { queries: { retry: false } } });

  // Cubre el cierre manual y la sesión vencida: otra persona en el mismo navegador
  // no debe ver datos privados que quedaron en caché.
  sessionStore.subscribe(() => {
    if (sessionStore.getSession() === null) {
      queryClient.clear();
    }
  });

  const auth: AuthHandler = {
    getToken: () => sessionStore.getSession()?.token ?? null,
    onUnauthorized: sessionStore.clear,
  };

  return {
    sessionStore,
    queryClient,
    usersApi: new UsersApi(new HttpClient(config.usersApiUrl, auth)),
    metadataApi: new MetadataApi(new HttpClient(config.metadataApiUrl, auth)),
    streamingApi: new StreamingApi(
      new HttpClient(config.streamingApiUrl, auth),
      config.streamingApiUrl,
    ),
  };
}
