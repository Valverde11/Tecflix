import http from 'k6/http';
import { check, fail, group } from 'k6';
import {
  authCredentials,
  requiredEnv,
  targetUrl,
  testProfile,
} from './lib/config.js';

const usersUrl = targetUrl('USERS_URL', 'http://127.0.0.1:8081');
const metadataUrl = targetUrl('METADATA_URL', 'http://127.0.0.1:8082');
const streamingUrl = targetUrl('STREAMING_URL', 'http://127.0.0.1:8083');
const videoId = requiredEnv('LOADTEST_VIDEO_ID');

if (!/^[a-f0-9]{32}$/.test(videoId)) {
  throw new Error('LOADTEST_VIDEO_ID debe tener 32 caracteres hexadecimales en minúscula');
}

export const options = testProfile('journey');

function expectOk(response, label) {
  check(response, {
    [`${label} responde HTTP 200`]: (result) => result.status === 200,
  });
}

function authParams(name, token) {
  return {
    headers: {
      Accept: 'application/json',
      Authorization: `Bearer ${token}`,
    },
    timeout: '10s',
    tags: { name },
  };
}

export function setup() {
  const credentials = authCredentials();
  const response = http.post(
    `${usersUrl}/api/auth/login`,
    JSON.stringify(credentials),
    {
      headers: { 'Content-Type': 'application/json', Accept: 'application/json' },
      timeout: '10s',
      tags: { name: 'POST /api/auth/login', workload: 'setup' },
    },
  );

  if (response.status !== 200) {
    fail(`Login de preparación falló con HTTP ${response.status}`);
  }

  const payload = JSON.parse(response.body);
  if (!payload.token || !payload.user || payload.user.id === undefined) {
    fail('La respuesta de login no contiene token e id de usuario');
  }

  return { token: payload.token, userId: payload.user.id };
}

export default function (data) {
  group('Tecflix: recorrido funcional de solo lectura', () => {
    const profile = http.get(
      `${usersUrl}/api/users/me`,
      authParams('GET /api/users/me', data.token),
    );
    expectOk(profile, 'Perfil del usuario');

    const quota = http.get(
      `${usersUrl}/api/users/${data.userId}/quota`,
      authParams('GET /api/users/:id/quota', data.token),
    );
    expectOk(quota, 'Cuota del usuario');

    const catalog = http.get(
      `${metadataUrl}/api/metadata/videos?page=1&page_size=20&mine=true`,
      authParams('GET /api/metadata/videos', data.token),
    );
    expectOk(catalog, 'Catálogo de metadata');

    const metadata = http.get(
      `${metadataUrl}/api/metadata/videos/${videoId}`,
      authParams('GET /api/metadata/videos/:id', data.token),
    );
    expectOk(metadata, 'Metadata del video');

    const status = http.get(
      `${streamingUrl}/api/videos/${videoId}`,
      authParams('GET /api/videos/:id', data.token),
    );
    expectOk(status, 'Estado de streaming');

    const manifest = http.get(
      `${streamingUrl}/api/videos/${videoId}/index.m3u8`,
      authParams('GET /api/videos/:id/index.m3u8', data.token),
    );
    const manifestOk = check(manifest, {
      'Manifiesto HLS responde HTTP 200': (result) => result.status === 200,
      'Manifiesto HLS contiene un segmento': (result) =>
        /(?:^|\/)(segment_[0-9]{5}\.ts)$/m.test(result.body || ''),
    });

    if (manifestOk) {
      const segment = (manifest.body.match(
        /(?:^|\/)(segment_[0-9]{5}\.ts)$/m,
      ) || [])[1];
      const segmentResponse = http.get(
        `${streamingUrl}/api/videos/${videoId}/segments/${segment}`,
        authParams('GET /api/videos/:id/segments/:segment', data.token),
      );
      check(segmentResponse, {
        'Segmento HLS responde HTTP 200': (result) => result.status === 200,
      });
    }
  });
}
