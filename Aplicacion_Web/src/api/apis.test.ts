import { afterEach, beforeEach, describe, expect, it, vi } from 'vitest';
import type { Video, VideoDetails } from '../types/video';
import { HttpClient } from './HttpClient';
import { MetadataApi } from './MetadataApi';
import { StreamingApi } from './StreamingApi';
import { UsersApi } from './UsersApi';

const BASE_URL = 'http://api.test';
const VIDEO_ID = 'a'.repeat(32);

const user = {
  id: 7,
  username: 'ana',
  email: 'ana@tec.cr',
  quotaBytes: 1000,
  usedBytes: 10,
  createdAt: '2026-11-03T15:04:05Z',
};

const video: Video = {
  videoId: VIDEO_ID,
  ownerId: 7,
  author: 'ana',
  title: 'Árboles B',
  description: 'Clase',
  topic: 'Datos',
  tags: ['btree'],
  durationSeconds: 60,
  sizeBytes: 500,
  visibility: 'public',
  uploadedAt: '2026-11-03T15:04:05Z',
};

const metadataVideo = {
  id: VIDEO_ID,
  owner_id: '7',
  owner_name: 'ana',
  title: 'Árboles B',
  description: 'Clase',
  topic: 'Datos',
  tags: ['btree'],
  duration_seconds: 60,
  size_bytes: 500,
  visibility: 'public',
  uploaded_at: '2026-11-03T15:04:05Z',
  updated_at: '2026-11-03T15:04:05Z',
  genres: [],
  cast: [],
};

const details: VideoDetails = {
  title: 'Nuevo',
  description: '',
  topic: 'Datos',
  tags: [],
  visibility: 'private',
};

const fetchMock = vi.fn<typeof fetch>();
const http = new HttpClient(BASE_URL, { getToken: () => null, onUnauthorized: vi.fn() });

function respondWith(body: unknown, status = 200) {
  fetchMock.mockResolvedValue(new Response(JSON.stringify(body), { status }));
}

function lastCall(): { url: unknown; method: unknown; body: unknown } {
  const [url, init] = fetchMock.mock.lastCall ?? [];
  return { url, method: init?.method, body: init?.body };
}

beforeEach(() => {
  vi.stubGlobal('fetch', fetchMock);
});

afterEach(() => {
  vi.unstubAllGlobals();
  fetchMock.mockReset();
});

describe('UsersApi', () => {
  const api = new UsersApi(http);

  it('registra un usuario', async () => {
    respondWith(user, 201);
    const newUser = { username: 'ana', email: 'ana@tec.cr', password: 'secreta123' };

    await expect(api.register(newUser)).resolves.toEqual(user);
    expect(lastCall()).toEqual({
      url: `${BASE_URL}/api/users`,
      method: 'POST',
      body: JSON.stringify(newUser),
    });
  });

  it('inicia sesión y devuelve token y usuario', async () => {
    respondWith({ token: 'jwt', user });

    const session = await api.login({ username: 'ana', password: 'secreta123' });

    expect(session).toEqual({ token: 'jwt', user });
    expect(lastCall().url).toBe(`${BASE_URL}/api/auth/login`);
  });

  it('rechaza un login cuya respuesta no trae usuario', async () => {
    respondWith({ token: 'jwt' });

    await expect(api.login({ username: 'ana', password: 'x' })).rejects.toMatchObject({
      code: 'invalid_response',
    });
  });

  it('consulta el usuario actual', async () => {
    respondWith(user);

    await expect(api.getCurrentUser()).resolves.toEqual(user);
    expect(lastCall().url).toBe(`${BASE_URL}/api/users/me`);
  });

  it('consulta la cuota', async () => {
    respondWith({ quotaBytes: 1000, usedBytes: 10 });

    await expect(api.getQuota(7)).resolves.toEqual({ quotaBytes: 1000, usedBytes: 10 });
    expect(lastCall().url).toBe(`${BASE_URL}/api/users/7/quota`);
  });
});

describe('MetadataApi', () => {
  const api = new MetadataApi(http);

  it('traduce la paginación y el orden al contrato snake_case de metadata', async () => {
    respondWith({ items: [metadataVideo], page: 2, page_size: 20, total: 21 });

    const page = await api.listVideos({ page: 2, pageSize: 20, sortBy: 'title', order: 'desc' });

    expect(page).toEqual({ items: [video], page: 2, pageSize: 20, total: 21 });
    expect(lastCall().url).toBe(
      `${BASE_URL}/api/metadata/videos?page=2&page_size=20&sort=title&order=desc`,
    );
  });

  it('traduce uploadedAt al valor uploaded_at esperado por metadata', async () => {
    respondWith({ items: [metadataVideo], page: 1, page_size: 20, total: 1 });

    await api.listVideos({ page: 1, pageSize: 20, sortBy: 'uploadedAt', order: 'desc' });

    expect(lastCall().url).toBe(
      `${BASE_URL}/api/metadata/videos?page=1&page_size=20&sort=uploaded_at&order=desc`,
    );
  });

  it('convierte la respuesta snake_case al modelo camelCase de la web', async () => {
    respondWith(metadataVideo);

    await expect(api.getVideo(VIDEO_ID)).resolves.toEqual(video);
  });

  it('rechaza owner_id que no sea decimal o exceda el entero seguro de JavaScript', async () => {
    respondWith({ ...metadataVideo, owner_id: '7x' });
    await expect(api.getVideo(VIDEO_ID)).rejects.toMatchObject({ code: 'invalid_response' });

    respondWith({ ...metadataVideo, owner_id: '9007199254740992' });
    await expect(api.getVideo(VIDEO_ID)).rejects.toMatchObject({ code: 'invalid_response' });
  });

  it('crea, edita y borra un video con los métodos y campos del contrato real', async () => {
    respondWith(metadataVideo, 201);
    await api.createVideo({ ...details, videoId: VIDEO_ID, durationSeconds: 60, sizeBytes: 500 });
    expect(lastCall()).toEqual({
      url: `${BASE_URL}/api/metadata/videos`,
      method: 'POST',
      body: JSON.stringify({
        id: VIDEO_ID,
        title: 'Nuevo',
        description: '',
        topic: 'Datos',
        tags: [],
        duration_seconds: 60,
        size_bytes: 500,
        visibility: 'private',
      }),
    });

    respondWith(metadataVideo);
    await api.updateVideo(VIDEO_ID, details);
    expect(lastCall()).toMatchObject({
      url: `${BASE_URL}/api/metadata/videos/${VIDEO_ID}`,
      method: 'PATCH',
      body: JSON.stringify(details),
    });

    fetchMock.mockResolvedValue(new Response(null, { status: 204 }));
    await api.deleteVideo(VIDEO_ID);
    expect(lastCall().method).toBe('DELETE');
  });

  it('escapa el id que viene de la URL', async () => {
    respondWith(metadataVideo);

    await api.getVideo('../users/1');

    expect(lastCall().url).toBe(`${BASE_URL}/api/metadata/videos/..%2Fusers%2F1`);
  });
});

describe('StreamingApi', () => {
  const api = new StreamingApi(http, BASE_URL);

  it('consulta el estado de la transcodificación', async () => {
    respondWith({ status: 'processing' });

    await expect(api.getStatus(VIDEO_ID)).resolves.toBe('processing');
    expect(lastCall().url).toBe(`${BASE_URL}/api/videos/${VIDEO_ID}`);
  });

  it('arma la URL del manifiesto HLS', () => {
    expect(api.getManifestUrl(VIDEO_ID)).toBe(`${BASE_URL}/api/videos/${VIDEO_ID}/index.m3u8`);
  });

  it('borra los archivos del video', async () => {
    fetchMock.mockResolvedValue(new Response(null, { status: 204 }));

    await api.deleteVideo(VIDEO_ID);

    expect(lastCall()).toMatchObject({
      url: `${BASE_URL}/api/videos/${VIDEO_ID}`,
      method: 'DELETE',
    });
  });
});
