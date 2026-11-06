import { http, HttpResponse } from 'msw';
import type { Credentials, NewUser } from '../types/user';
import type { SortOrder, VideoListQuery, VideoSortField } from '../types/video';
import {
  TAKEN_EMAIL,
  TAKEN_USERNAME,
  TEST_API_CONFIG,
  TEST_PASSWORD,
  testSession,
  testUser,
} from './fixtures';
import { queryCatalog, videoCatalog } from './videoCatalog';

const { usersApiUrl, metadataApiUrl, streamingApiUrl } = TEST_API_CONFIG;

function apiError(status: number, error: string, message: string) {
  return HttpResponse.json({ error, message }, { status });
}

const usersHandlers = [
  http.post(`${usersApiUrl}/api/users`, async ({ request }) => {
    const newUser = (await request.json()) as NewUser;
    if (newUser.username === TAKEN_USERNAME) {
      return apiError(409, 'username_taken', 'El nombre de usuario ya existe');
    }
    if (newUser.email === TAKEN_EMAIL) {
      return apiError(409, 'email_taken', 'El correo ya está registrado');
    }
    const { username, email } = newUser;
    return HttpResponse.json({ ...testUser, username, email }, { status: 201 });
  }),
  http.post(`${usersApiUrl}/api/auth/login`, async ({ request }) => {
    const { username, password } = (await request.json()) as Credentials;
    if (password !== TEST_PASSWORD) {
      return apiError(401, 'invalid_credentials', 'Usuario o contraseña incorrectos');
    }
    return HttpResponse.json({ ...testSession, user: { ...testUser, username } });
  }),
  http.get(`${usersApiUrl}/api/users/me`, () => HttpResponse.json(testUser)),
  http.get(`${usersApiUrl}/api/users/:id/quota`, () => {
    const { quotaBytes, usedBytes } = testUser;
    return HttpResponse.json({ quotaBytes, usedBytes });
  }),
];

function parseListQuery(url: string): VideoListQuery {
  const query = new URL(url).searchParams;
  return {
    page: Number(query.get('page')),
    pageSize: Number(query.get('pageSize')),
    sortBy: query.get('sortBy') as VideoSortField,
    order: query.get('order') as SortOrder,
  };
}

/** Recibe la URL porque también se usa en desarrollo, contra la dirección real de metadata. */
export function createMetadataHandlers(baseUrl: string) {
  return [
    http.get(`${baseUrl}/api/metadata/videos`, ({ request }) =>
      HttpResponse.json(queryCatalog(videoCatalog, parseListQuery(request.url))),
    ),
    http.get(`${baseUrl}/api/metadata/videos/:videoId`, ({ params }) => {
      const video = videoCatalog.find(({ videoId }) => videoId === params.videoId);
      return video === undefined
        ? apiError(404, 'video_not_found', 'El video no existe')
        : HttpResponse.json(video);
    }),
  ];
}

const streamingHandlers = [
  http.get(`${streamingApiUrl}/api/videos/:videoId`, () => HttpResponse.json({ status: 'ready' })),
];

export const handlers = [
  ...usersHandlers,
  ...createMetadataHandlers(metadataApiUrl),
  ...streamingHandlers,
];
