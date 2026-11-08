import { describe, expect, it } from 'vitest';
import { HttpClient } from '../api/HttpClient';
import { MetadataApi } from '../api/MetadataApi';
import { StreamingApi } from '../api/StreamingApi';
import { UsersApi } from '../api/UsersApi';
import {
  TAKEN_EMAIL,
  TAKEN_USERNAME,
  TEST_API_CONFIG,
  TEST_PASSWORD,
  testSession,
  testUser,
  testVideo,
} from './fixtures';
import { videoCatalog } from './videoCatalog';

const anonymous = { getToken: () => null, onUnauthorized: () => undefined };
const usersApi = new UsersApi(new HttpClient(TEST_API_CONFIG.usersApiUrl, anonymous));
const metadataApi = new MetadataApi(new HttpClient(TEST_API_CONFIG.metadataApiUrl, anonymous));
const streamingApi = new StreamingApi(
  new HttpClient(TEST_API_CONFIG.streamingApiUrl, anonymous),
  TEST_API_CONFIG.streamingApiUrl,
);

const newUser = { username: 'luis', email: 'luis@tec.cr', password: TEST_PASSWORD };

// Si un simulador se aparta de los parsers, las pruebas de pantallas fallarían por error ajeno.
describe('servidor simulado', () => {
  it('registra y rechaza duplicados con los códigos de la API real', async () => {
    await expect(usersApi.register(newUser)).resolves.toMatchObject({ username: 'luis' });
    await expect(usersApi.register({ ...newUser, username: TAKEN_USERNAME })).rejects.toMatchObject(
      { status: 409, code: 'username_taken' },
    );
    await expect(usersApi.register({ ...newUser, email: TAKEN_EMAIL })).rejects.toMatchObject({
      status: 409,
      code: 'email_taken',
    });
  });

  it('valida la contraseña en el login', async () => {
    const credentials = { username: 'ana', password: TEST_PASSWORD };

    await expect(usersApi.login(credentials)).resolves.toEqual(testSession);
    await expect(usersApi.login({ ...credentials, password: 'otra' })).rejects.toMatchObject({
      status: 401,
      code: 'invalid_credentials',
    });
  });

  it('responde usuario y cuota', async () => {
    await expect(usersApi.getCurrentUser()).resolves.toEqual(testUser);
    await expect(usersApi.getQuota(testUser.id)).resolves.toMatchObject({ usedBytes: 0 });
  });

  it('ordena y pagina como lo haría metadata', async () => {
    const query = { page: 2, pageSize: 10, sortBy: 'title', order: 'asc' } as const;

    const page = await metadataApi.listVideos(query);

    expect(page).toMatchObject({ page: 2, pageSize: 10, total: videoCatalog.length });
    expect(page.items).toHaveLength(10);
    const titles = page.items.map(({ title }) => title);
    expect(titles).toEqual(titles.toSorted((a, b) => a.localeCompare(b, 'es')));
  });

  it('invierte el orden con desc', async () => {
    const query = { page: 1, pageSize: 25, sortBy: 'uploadedAt', order: 'desc' } as const;

    const { items } = await metadataApi.listVideos(query);

    expect(items[0]?.videoId).toBe(testVideo.videoId);
  });

  it('responde el detalle y 404 si el video no existe', async () => {
    await expect(metadataApi.getVideo(testVideo.videoId)).resolves.toEqual(testVideo);
    await expect(metadataApi.getVideo('f'.repeat(32))).rejects.toMatchObject({
      status: 404,
      code: 'video_not_found',
    });
  });

  it('responde el estado de la transcodificación', async () => {
    await expect(streamingApi.getStatus(testVideo.videoId)).resolves.toBe('ready');
  });
});
