import { http, HttpResponse } from 'msw';
import { beforeEach, describe, expect, it } from 'vitest';
import { TEST_API_CONFIG, testSession } from '../test/fixtures';
import { server } from '../test/server';
import { createServices } from './createServices';

const ME_URL = `${TEST_API_CONFIG.usersApiUrl}/api/users/me`;

function createLoggedInServices() {
  const services = createServices(TEST_API_CONFIG, window.sessionStorage);
  services.sessionStore.setSession(testSession);
  return services;
}

beforeEach(() => {
  window.sessionStorage.clear();
});

describe('createServices', () => {
  it('envía a las APIs el token de la sesión activa', async () => {
    let authorization: string | null = null;
    server.use(
      http.get(ME_URL, ({ request }) => {
        authorization = request.headers.get('Authorization');
        return HttpResponse.json(testSession.user);
      }),
    );

    await createLoggedInServices().usersApi.getCurrentUser();

    expect(authorization).toBe(`Bearer ${testSession.token}`);
  });

  it('cierra la sesión cuando la API rechaza el token', async () => {
    server.use(
      http.get(ME_URL, () => HttpResponse.json({ error: 'unauthorized' }, { status: 401 })),
    );
    const services = createLoggedInServices();

    await expect(services.usersApi.getCurrentUser()).rejects.toMatchObject({ status: 401 });

    expect(services.sessionStore.getSession()).toBeNull();
  });

  it('vacía la caché al cerrar sesión', () => {
    const services = createLoggedInServices();
    services.queryClient.setQueryData(['videos'], ['privado']);

    services.sessionStore.clear();

    expect(services.queryClient.getQueryData(['videos'])).toBeUndefined();
  });

  it('conserva la caché al iniciar sesión', () => {
    const services = createLoggedInServices();
    services.queryClient.setQueryData(['videos'], ['dato']);

    services.sessionStore.setSession(testSession);

    expect(services.queryClient.getQueryData(['videos'])).toEqual(['dato']);
  });
});
