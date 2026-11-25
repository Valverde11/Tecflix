const LOCAL_HOSTS = new Set([
  'localhost',
  'host.docker.internal',
  '::1',
]);

function isPrivateIPv4(host) {
  const parts = host.split('.');
  if (parts.length !== 4 || parts.some((part) => !/^\d{1,3}$/.test(part))) {
    return false;
  }

  const octets = parts.map(Number);
  if (octets.some((part) => part > 255)) return false;

  return (
    octets[0] === 10 ||
    octets[0] === 127 ||
    (octets[0] === 172 && octets[1] >= 16 && octets[1] <= 31) ||
    (octets[0] === 192 && octets[1] === 168)
  );
}

function isLocalHost(host) {
  return (
    LOCAL_HOSTS.has(host) ||
    host.endsWith('.localhost') ||
    host.endsWith('.local') ||
    host.endsWith('.test') ||
    !host.includes('.') ||
    isPrivateIPv4(host) ||
    /^f[cd][0-9a-f]{2}:/i.test(host)
  );
}

export function targetUrl(name, fallback) {
  const value = __ENV[name] || fallback;
  let parsed;

  try {
    parsed = new URL(value);
  } catch {
    throw new Error(`${name} debe ser una URL HTTP(S) válida`);
  }

  if (
    !['http:', 'https:'].includes(parsed.protocol) ||
    parsed.username ||
    parsed.password ||
    parsed.search ||
    parsed.hash
  ) {
    throw new Error(`${name} debe ser HTTP(S), sin credenciales, query ni fragmento`);
  }

  const host = parsed.hostname.replace(/^\[|\]$/g, '').toLowerCase();
  if (__ENV.LOADTEST_ALLOW_REMOTE !== 'true' && !isLocalHost(host)) {
    throw new Error(
      `${name} apunta a un host externo; use solo ambientes autorizados y defina ` +
        'LOADTEST_ALLOW_REMOTE=true para permitirlo explícitamente',
    );
  }

  return value.replace(/\/+$/, '');
}

export function requiredEnv(name) {
  const value = __ENV[name];
  if (!value) throw new Error(`Falta la variable requerida ${name}`);
  return value;
}

export function testProfile(workload) {
  const profile = __ENV.LOADTEST_PROFILE || 'smoke';
  if (!['smoke', 'capacity'].includes(profile)) {
    throw new Error('LOADTEST_PROFILE debe ser smoke o capacity');
  }

  const maxAllowed = workload === 'login' ? 10 : 50;
  const configuredMax = Number(__ENV.LOADTEST_MAX_VUS || maxAllowed);
  if (
    !Number.isInteger(configuredMax) ||
    configuredMax < 1 ||
    configuredMax > maxAllowed
  ) {
    throw new Error(
      `LOADTEST_MAX_VUS debe ser un entero entre 1 y ${maxAllowed} para ${workload}`,
    );
  }

  const stageDuration = Number(__ENV.LOADTEST_STAGE_DURATION_SECONDS || 30);
  if (
    profile === 'capacity' &&
    (!Number.isInteger(stageDuration) || stageDuration < 10 || stageDuration > 300)
  ) {
    throw new Error('LOADTEST_STAGE_DURATION_SECONDS debe ser un entero entre 10 y 300');
  }

  const stages =
    profile === 'smoke'
      ? [{ duration: '10s', target: 1 }]
      : [
          ...new Set(
            [5, 10, 20, 30, 40, configuredMax].map((target) =>
              Math.min(target, configuredMax),
            ),
          ),
        ].map((target) => ({
          duration: `${stageDuration}s`,
          target,
        }));
  const finalStages =
    profile === 'smoke' ? stages : [...stages, { duration: '10s', target: 0 }];

  return {
    scenarios: {
      [workload]: {
        executor: 'ramping-vus',
        startVUs: 1,
        stages: finalStages,
        gracefulRampDown: '5s',
      },
    },
    thresholds: {
      checks: ['rate==1'],
      http_req_failed: ['rate==0'],
    },
    tags: {
      workload,
      topology: __ENV.LOADTEST_RUN_LABEL || 'single',
    },
  };
}

export function authCredentials() {
  return {
    username: requiredEnv('LOADTEST_USERNAME'),
    password: requiredEnv('LOADTEST_PASSWORD'),
  };
}
