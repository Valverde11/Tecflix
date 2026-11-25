import http from 'k6/http';
import { check } from 'k6';
import { authCredentials, targetUrl, testProfile } from './lib/config.js';

const usersUrl = targetUrl('USERS_URL', 'http://127.0.0.1:8081');
const credentials = authCredentials();

export const options = testProfile('login');

export default function () {
  const response = http.post(
    `${usersUrl}/api/auth/login`,
    JSON.stringify(credentials),
    {
      headers: { 'Content-Type': 'application/json', Accept: 'application/json' },
      timeout: '10s',
      tags: { name: 'POST /api/auth/login' },
    },
  );

  check(response, {
    'Login responde HTTP 200': (result) => result.status === 200,
    'Login devuelve un token': (result) => {
      const payload = JSON.parse(result.body || '{}');
      return typeof payload.token === 'string' && payload.token.length > 0;
    },
  });
}
