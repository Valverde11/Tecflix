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

  it('responde la lista y el detalle de videos', async () => {
    const query = { page: 3, pageSize: 10, sortBy: 'title', order: 'asc' } as const;

    await expect(metadataApi.listVideos(query)).resolves.toEqual({
      items: [testVideo],
      page: 3,
      pageSize: 10,
      total: 1,
    });
    await expect(metadataApi.getVideo(testVideo.videoId)).resolves.toEqual(testVideo);
  });

  it('responde el estado de la transcodificación', async () => {
    await expect(streamingApi.getStatus(testVideo.videoId)).resolves.toBe('ready');
  });
});
