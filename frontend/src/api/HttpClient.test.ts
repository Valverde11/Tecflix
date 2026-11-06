import { afterEach, beforeEach, describe, expect, it, vi } from 'vitest';
import { ApiError, NETWORK_ERROR_STATUS } from './ApiError';
import { HttpClient } from './HttpClient';

const BASE_URL = 'http://api.test';
const TOKEN = 'token-de-prueba';

const fetchMock = vi.fn<typeof fetch>();
const onUnauthorized = vi.fn<() => void>();

function jsonResponse(status: number, body: unknown): Response {
  return new Response(JSON.stringify(body), { status });
}

function clientWith(token: string | null): HttpClient {
  return new HttpClient(BASE_URL, { getToken: () => token, onUnauthorized });
}

function lastRequest(): { url: string; init: RequestInit } {
  const call = fetchMock.mock.lastCall;
  if (call === undefined) {
    throw new Error('No se hizo ninguna petición');
  }
  const [url, init] = call;
  if (typeof url !== 'string') {
    throw new Error('Se esperaba una URL de texto');
  }
  return { url, init: init ?? {} };
}

const identity = (data: unknown): unknown => data;

async function captureError(action: Promise<unknown>): Promise<ApiError> {
  const error: unknown = await action.then(
    () => null,
    (reason: unknown) => reason,
  );
  if (!(error instanceof ApiError)) {
    throw new Error('Se esperaba un ApiError');
  }
  return error;
}

beforeEach(() => {
  vi.stubGlobal('fetch', fetchMock);
});

afterEach(() => {
  vi.unstubAllGlobals();
  fetchMock.mockReset();
  onUnauthorized.mockReset();
});

describe('HttpClient', () => {
  it('agrega el token cuando hay sesión', async () => {
    fetchMock.mockResolvedValue(jsonResponse(200, {}));

    await clientWith(TOKEN).get('/api/x', identity);

    expect(lastRequest().init.headers).toEqual({ Authorization: `Bearer ${TOKEN}` });
  });

  it('no envía Authorization sin sesión', async () => {
    fetchMock.mockResolvedValue(jsonResponse(200, {}));

    await clientWith(null).get('/api/x', identity);

    expect(lastRequest().init.headers).toEqual({});
  });

  it('codifica los parámetros de la consulta', async () => {
    fetchMock.mockResolvedValue(jsonResponse(200, {}));

    await clientWith(null).get('/api/x', identity, { page: 2, sortBy: 'a b' });

    expect(lastRequest().url).toBe(`${BASE_URL}/api/x?page=2&sortBy=a+b`);
  });

  it('envía el cuerpo como JSON y aplica el parser', async () => {
    fetchMock.mockResolvedValue(jsonResponse(201, { id: 7 }));

    const result = await clientWith(null).post('/api/x', { a: 1 }, () => 'parseado');

    expect(result).toBe('parseado');
    expect(lastRequest().init).toMatchObject({
      method: 'POST',
      body: '{"a":1}',
      headers: { 'Content-Type': 'application/json' },
    });
  });

  it('usa PUT para actualizar', async () => {
    fetchMock.mockResolvedValue(jsonResponse(200, {}));

    await clientWith(null).put('/api/x', {}, identity);

    expect(lastRequest().init.method).toBe('PUT');
  });

  it('acepta 204 sin cuerpo al borrar', async () => {
    fetchMock.mockResolvedValue(new Response(null, { status: 204 }));

    await expect(clientWith(null).delete('/api/x')).resolves.toBeUndefined();
    expect(lastRequest().init.method).toBe('DELETE');
  });

  it('traduce el error común de la API', async () => {
    const body = { error: 'username_taken', message: 'Ya existe' };
    fetchMock.mockResolvedValue(jsonResponse(409, body));

    const error = await captureError(clientWith(null).get('/api/x', identity));

    expect(error).toMatchObject({ status: 409, code: 'username_taken', message: 'Ya existe' });
  });

  it('acepta errores sin message, como los de streaming', async () => {
    fetchMock.mockResolvedValue(jsonResponse(404, { error: 'video_not_found' }));

    const error = await captureError(clientWith(null).get('/api/x', identity));

    expect(error.code).toBe('video_not_found');
    expect(error.message).toContain('404');
  });

  it('da un error común aunque el cuerpo no sea JSON', async () => {
    fetchMock.mockResolvedValue(new Response('<html>Bad Gateway</html>', { status: 502 }));

    const error = await captureError(clientWith(null).get('/api/x', identity));

    expect(error).toMatchObject({ status: 502, code: 'http_error' });
  });

  it('avisa la sesión vencida si un 401 llega con token', async () => {
    fetchMock.mockResolvedValue(jsonResponse(401, { error: 'unauthorized' }));

    await captureError(clientWith(TOKEN).get('/api/x', identity));

    expect(onUnauthorized).toHaveBeenCalledOnce();
  });

  it('no cierra sesión por un 401 sin token, como una clave incorrecta', async () => {
    fetchMock.mockResolvedValue(jsonResponse(401, { error: 'invalid_credentials' }));

    const error = await captureError(clientWith(null).post('/api/login', {}, identity));

    expect(error.code).toBe('invalid_credentials');
    expect(onUnauthorized).not.toHaveBeenCalled();
  });

  it('no cierra sesión por un 403', async () => {
    fetchMock.mockResolvedValue(jsonResponse(403, { error: 'forbidden' }));

    await captureError(clientWith(TOKEN).get('/api/x', identity));

    expect(onUnauthorized).not.toHaveBeenCalled();
  });

  it('convierte una falla de red en un error común', async () => {
    fetchMock.mockRejectedValue(new TypeError('Failed to fetch'));

    const error = await captureError(clientWith(null).get('/api/x', identity));

    expect(error).toMatchObject({ status: NETWORK_ERROR_STATUS, code: 'network_error' });
  });

  it('rechaza una respuesta exitosa que no es JSON', async () => {
    fetchMock.mockResolvedValue(new Response('hola', { status: 200 }));

    const error = await captureError(clientWith(null).get('/api/x', identity));

    expect(error.code).toBe('invalid_response');
  });

  it('incluye en el error lo que el parser no reconoció', async () => {
    fetchMock.mockResolvedValue(jsonResponse(200, {}));
    const parse = (): never => {
      throw new Error('falta id');
    };

    const error = await captureError(clientWith(null).get('/api/x', parse));

    expect(error.code).toBe('invalid_response');
    expect(error.message).toContain('falta id');
  });
});
