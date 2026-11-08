export interface ApiConfig {
  readonly usersApiUrl: string;
  readonly metadataApiUrl: string;
  readonly streamingApiUrl: string;
}

type EnvSource = Readonly<Record<string, unknown>>;

const ALLOWED_PROTOCOLS = ['http:', 'https:'];

function readApiUrl(env: EnvSource, name: string): string {
  const value = env[name];
  if (typeof value !== 'string' || value === '') {
    throw new Error(`Falta la variable de entorno ${name} (ver .env.example)`);
  }
  if (!URL.canParse(value) || !ALLOWED_PROTOCOLS.includes(new URL(value).protocol)) {
    throw new Error(`La variable de entorno ${name} no es una URL http(s): ${value}`);
  }
  // Las rutas empiezan con "/": una barra final daría URLs con "//".
  return value.replace(/\/+$/, '');
}

export function loadApiConfig(env: EnvSource): ApiConfig {
  return {
    usersApiUrl: readApiUrl(env, 'VITE_USERS_API_URL'),
    metadataApiUrl: readApiUrl(env, 'VITE_METADATA_API_URL'),
    streamingApiUrl: readApiUrl(env, 'VITE_STREAMING_API_URL'),
  };
}
