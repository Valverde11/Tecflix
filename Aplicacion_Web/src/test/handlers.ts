import { http, HttpResponse } from 'msw';
import type { Credentials, NewUser } from '../types/user';
import type {
  SortOrder,
  VideoDetails,
  VideoListQuery,
  VideoSortField,
  Video,
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
  const sort = query.get('sort') ?? 'uploaded_at';
  return {
    page: Number(query.get('page') ?? 1),
    pageSize: Number(query.get('page_size') ?? 20),
    sortBy: (sort === 'uploaded_at' ? 'uploadedAt' : sort) as VideoSortField,
    order: (query.get('order') ?? 'desc') as SortOrder,
  };
}

export function toMetadataJson(video: Video) {
  return {
    id: video.videoId,
    owner_id: String(video.ownerId),
    owner_name: video.author,
    title: video.title,
    description: video.description,
    topic: video.topic,
    tags: video.tags,
    duration_seconds: video.durationSeconds,
    size_bytes: video.sizeBytes,
    visibility: video.visibility,
    uploaded_at: video.uploadedAt,
    updated_at: video.uploadedAt,
    genres: [],
    cast: [],
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
    http.get(videosUrl, ({ request }) => {
      const page = queryCatalog(videoStore.all(), parseListQuery(request.url));
      return HttpResponse.json({
        items: page.items.map(toMetadataJson),
        page: page.page,
        page_size: page.pageSize,
        total: page.total,
      });
    }),
    http.post(videosUrl, async ({ request }) => {
      const body = (await request.json()) as Record<string, unknown>;
      const owner = ownerOf(request);
      const video: Video = {
        videoId: String(body.id),
        ownerId: owner.ownerId,
        author: owner.author,
        title: String(body.title),
        description: typeof body.description === 'string' ? body.description : '',
        topic: typeof body.topic === 'string' ? body.topic : '',
        tags: Array.isArray(body.tags) ? body.tags.map(String) : [],
        durationSeconds: Number(body.duration_seconds),
        sizeBytes: Number(body.size_bytes),
        visibility: body.visibility === 'public' ? 'public' : 'private',
        uploadedAt: new Date().toISOString(),
      };
      videoStore.add(video);
      return HttpResponse.json(toMetadataJson(video), { status: 201 });
    }),
    http.get<{ videoId: string }>(`${videosUrl}/:videoId`, ({ params }) => {
      const video = videoStore.find(params.videoId);
      return video === undefined ? notFound() : HttpResponse.json(toMetadataJson(video));
    }),
    http.patch<{ videoId: string }>(`${videosUrl}/:videoId`, async ({ params, request }) => {
      const changes = (await request.json()) as VideoDetails;
      const video = videoStore.update(params.videoId, changes);
      return video === undefined ? notFound() : HttpResponse.json(toMetadataJson(video));
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
  'Access-Control-Allow-Methods': 'GET, POST, PATCH, DELETE, OPTIONS',
};

export function createStreamingHandlers(baseUrl: string) {
  const videosUrl = `${baseUrl}/api/videos`;
  return [
    http.options(videosUrl, () => new HttpResponse(null, { status: 204, headers: CORS_HEADERS })),
    http.post(videosUrl, async ({ request }) => {
      const body = await request.arrayBuffer();
      return HttpResponse.json(
        { id: UPLOADED_VIDEO_ID, status: 'queued', size_bytes: body.byteLength },
        { status: 202, headers: CORS_HEADERS },
      );
    }),
    http.get(`${videosUrl}/:videoId`, () => HttpResponse.json({ status: 'ready' })),
    http.delete(`${videosUrl}/:videoId`, () => new HttpResponse(null, { status: 204 })),
  ];
}

export const handlers = [
  ...usersHandlers,
  ...createMetadataHandlers(metadataApiUrl),
  ...createStreamingHandlers(streamingApiUrl),
];
