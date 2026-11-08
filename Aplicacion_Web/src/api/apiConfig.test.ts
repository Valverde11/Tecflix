import { describe, expect, it } from 'vitest';
import { loadApiConfig } from './apiConfig';

const validEnv = {
  VITE_USERS_API_URL: 'http://localhost:8081',
  VITE_METADATA_API_URL: 'http://localhost:8082',
  VITE_STREAMING_API_URL: 'https://streaming.example.com',
};

describe('loadApiConfig', () => {
  it('lee las tres URLs', () => {
    expect(loadApiConfig(validEnv)).toEqual({
      usersApiUrl: 'http://localhost:8081',
      metadataApiUrl: 'http://localhost:8082',
      streamingApiUrl: 'https://streaming.example.com',
    });
  });

  it('quita las barras finales', () => {
    const config = loadApiConfig({ ...validEnv, VITE_USERS_API_URL: 'http://localhost:8081//' });

    expect(config.usersApiUrl).toBe('http://localhost:8081');
  });

  it.each(['VITE_USERS_API_URL', 'VITE_METADATA_API_URL', 'VITE_STREAMING_API_URL'])(
    'falla nombrando %s si no existe',
    (name) => {
      const env = Object.fromEntries(Object.entries(validEnv).filter(([key]) => key !== name));

      expect(() => loadApiConfig(env)).toThrow(`Falta la variable de entorno ${name}`);
    },
  );

  it('falla si la variable está vacía', () => {
    expect(() => loadApiConfig({ ...validEnv, VITE_USERS_API_URL: '' })).toThrow('Falta');
  });

  it.each(['localhost:8081', 'no es url', 'ftp://localhost:8081'])('rechaza "%s"', (url) => {
    expect(() => loadApiConfig({ ...validEnv, VITE_USERS_API_URL: url })).toThrow(
      'no es una URL http(s)',
    );
  });
});
