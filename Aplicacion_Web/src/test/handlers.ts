import { http, HttpResponse } from 'msw';
import type { Credentials, NewUser } from '../types/user';
import type {
  NewVideo,
  SortOrder,
  VideoDetails,
  VideoListQuery,
  VideoSortField,
} from '../types/video';
import {
  TAKEN_EMAIL,
  TAKEN_USERNAME,
  TEST_API_CONFIG,
  TEST_PASSWORD,
  testSession,
  testUser,
  UPLOADED_VIDEO_ID,
} from './fixtures';
import { FakeVideoStore, queryCatalog } from './videoCatalog';

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

export const videoStore = new FakeVideoStore();

/** El dueño sale del token, como en metadata; con el token falso de las pruebas, es testUser. */
function ownerOf(request: Request) {
  const payload = request.headers.get('Authorization')?.split('.')[1];
  if (payload === undefined) {
    return { ownerId: testUser.id, author: testUser.username };
  }
  const claims = JSON.parse(atob(payload.replace(/-/g, '+').replace(/_/g, '/'))) as {
    sub: string;
    username: string;
  };
  return { ownerId: Number(claims.sub), author: claims.username };
}

function notFound() {
  return apiError(404, 'video_not_found', 'El video no existe');
}

/** Reciben la URL porque también se usan en desarrollo, contra las direcciones configuradas. */
export function createMetadataHandlers(baseUrl: string) {
  const videosUrl = `${baseUrl}/api/metadata/videos`;
  return [
    http.get(videosUrl, ({ request }) =>
      HttpResponse.json(queryCatalog(videoStore.all(), parseListQuery(request.url))),
    ),
    http.post(videosUrl, async ({ request }) => {
      const video = {
        ...((await request.json()) as NewVideo),
        ...ownerOf(request),
        status: 'queued' as const,
        uploadedAt: new Date().toISOString(),
      };
      videoStore.add(video);
      return HttpResponse.json(video, { status: 201 });
    }),
    http.get<{ videoId: string }>(`${videosUrl}/:videoId`, ({ params }) => {
      const video = videoStore.find(params.videoId);
      return video === undefined ? notFound() : HttpResponse.json(video);
    }),
    http.put<{ videoId: string }>(`${videosUrl}/:videoId`, async ({ params, request }) => {
      const changes = (await request.json()) as VideoDetails;
      const video = videoStore.update(params.videoId, changes);
      return video === undefined ? notFound() : HttpResponse.json(video);
    }),
    http.delete<{ videoId: string }>(`${videosUrl}/:videoId`, ({ params }) => {
      videoStore.remove(params.videoId);
      return new HttpResponse(null, { status: 204 });
    }),
  ];
}

/**
 * La subida lleva Authorization y un Content-Type que obligan al navegador a preguntar antes con
 * OPTIONS. El XMLHttpRequest de jsdom lo hace igual que uno real, y streaming debe responderlo.
 */
export const CORS_HEADERS = {
  'Access-Control-Allow-Origin': '*',
  'Access-Control-Allow-Headers': 'Authorization, Content-Type',
  'Access-Control-Allow-Methods': 'GET, POST, DELETE',
};

export function createStreamingHandlers(baseUrl: string) {
  const videosUrl = `${baseUrl}/api/videos`;
  return [
    http.options(videosUrl, () => new HttpResponse(null, { status: 204, headers: CORS_HEADERS })),
    http.post(videosUrl, () =>
      HttpResponse.json(
        { id: UPLOADED_VIDEO_ID, status: 'queued' },
        { status: 202, headers: CORS_HEADERS },
      ),
    ),
    http.get(`${videosUrl}/:videoId`, () => HttpResponse.json({ status: 'ready' })),
    http.delete(`${videosUrl}/:videoId`, () => new HttpResponse(null, { status: 204 })),
  ];
}

export const handlers = [
  ...usersHandlers,
  ...createMetadataHandlers(metadataApiUrl),
  ...createStreamingHandlers(streamingApiUrl),
];
