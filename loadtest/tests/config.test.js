import assert from 'node:assert/strict';
import { afterEach, test } from 'node:test';

globalThis.__ENV = {};
const { targetUrl, testProfile } = await import('../k6/lib/config.js');

afterEach(() => {
  globalThis.__ENV = {};
});

test('allows local and private-network targets by default', () => {
  globalThis.__ENV = {
    USERS_URL: 'http://127.0.0.1:8081',
    METADATA_URL: 'http://10.2.3.4:8082',
    STREAMING_URL: 'http://[fd12::1]:8083',
  };

  assert.equal(targetUrl('USERS_URL', ''), 'http://127.0.0.1:8081');
  assert.equal(targetUrl('METADATA_URL', ''), 'http://10.2.3.4:8082');
  assert.equal(targetUrl('STREAMING_URL', ''), 'http://[fd12::1]:8083');
});

test('requires explicit opt-in for public hosts', () => {
  globalThis.__ENV = { USERS_URL: 'https://api.example.com' };
  assert.throws(() => targetUrl('USERS_URL', ''), /LOADTEST_ALLOWED_HOSTS/);

  globalThis.__ENV.LOADTEST_ALLOWED_HOSTS = 'api.example.com';
  assert.equal(targetUrl('USERS_URL', ''), 'https://api.example.com');
  globalThis.__ENV.USERS_URL = 'https://other.example.com';
  assert.throws(() => targetUrl('USERS_URL', ''), /LOADTEST_ALLOWED_HOSTS/);
});

test('rejects credentials and query strings in target URLs', () => {
  globalThis.__ENV = { USERS_URL: 'http://user:pass@localhost:8081' };
  assert.throws(() => targetUrl('USERS_URL', ''), /sin credenciales/);

  globalThis.__ENV.USERS_URL = 'http://localhost:8081?debug=true';
  assert.throws(() => targetUrl('USERS_URL', ''), /sin credenciales/);
});

test('provides a one-user smoke profile and caps capacity profiles', () => {
  globalThis.__ENV = {};
  const smoke = testProfile('journey');
  assert.deepEqual(smoke.scenarios.journey.stages, [
    { duration: '10s', target: 1 },
  ]);

  globalThis.__ENV = {
    LOADTEST_PROFILE: 'capacity',
    LOADTEST_MAX_VUS: '51',
    LOADTEST_LOGIN_MAX_VUS: '11',
  };
  assert.throws(() => testProfile('journey'), /entre 1 y 50/);
  assert.throws(() => testProfile('login'), /entre 1 y 10/);
});

test('ramps capacity through the configured concurrency levels', () => {
  globalThis.__ENV = {
    LOADTEST_PROFILE: 'capacity',
    LOADTEST_MAX_VUS: '20',
    LOADTEST_STAGE_DURATION_SECONDS: '60',
  };

  assert.deepEqual(testProfile('journey').scenarios.journey.stages, [
    { duration: '60s', target: 5 },
    { duration: '60s', target: 10 },
    { duration: '60s', target: 20 },
    { duration: '10s', target: 0 },
  ]);
});

test('rejects capacity stages outside the supported duration range', () => {
  globalThis.__ENV = {
    LOADTEST_PROFILE: 'capacity',
    LOADTEST_STAGE_DURATION_SECONDS: '301',
  };
  assert.throws(
    () => testProfile('journey'),
    /LOADTEST_STAGE_DURATION_SECONDS debe ser un entero entre 10 y 300/,
  );
});
