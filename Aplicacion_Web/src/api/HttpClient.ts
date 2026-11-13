import { ApiError, NETWORK_ERROR_STATUS } from './ApiError';

const HTTP_UNAUTHORIZED = 401;
const UPLOAD_CONTENT_TYPE = 'application/octet-stream';

export const UPLOAD_CANCELLED = 'upload_cancelled';

export interface AuthHandler {
  getToken(): string | null;
  onUnauthorized(): void;
}

/** Convierte el JSON recibido en un tipo conocido, o lanza si no tiene esa forma. */
export type Parser<T> = (data: unknown) => T;

export type QueryParams = Readonly<Record<string, string | number>>;

export interface UploadOptions {
  /** Recibe la fracción enviada, de 0 a 1. */
  readonly onProgress: (fraction: number) => void;
  readonly signal: AbortSignal;
}

function networkError(): ApiError {
  return new ApiError(NETWORK_ERROR_STATUS, 'network_error', 'No se pudo conectar con el servidor');
}

function isRecord(data: unknown): data is Record<string, unknown> {
  return typeof data === 'object' && data !== null;
}

async function toApiError(response: Response): Promise<ApiError> {
  // Un proxy o un servidor caído pueden responder sin JSON; aun así hay que dar un error común.
  const body: unknown = await response.json().catch(() => null);
  const code = isRecord(body) && typeof body.error === 'string' ? body.error : 'http_error';
  const message =
    isRecord(body) && typeof body.message === 'string'
      ? body.message
      : `El servidor respondió con el estado ${String(response.status)}`;
  return new ApiError(response.status, code, message);
}

/** Único punto de la web que hace `fetch`: agrega el token y unifica los errores. */
export class HttpClient {
  private readonly baseUrl: string;
  private readonly auth: AuthHandler;

  constructor(baseUrl: string, auth: AuthHandler) {
    this.baseUrl = baseUrl;
    this.auth = auth;
  }

  async get<T>(path: string, parse: Parser<T>, query: QueryParams = {}): Promise<T> {
    const entries = Object.entries(query).map(([key, value]) => [key, String(value)]);
    const search = new URLSearchParams(entries).toString();
    const response = await this.send('GET', search === '' ? path : `${path}?${search}`);
    return this.readJson(response, parse);
  }

  async post<T>(path: string, body: unknown, parse: Parser<T>): Promise<T> {
    return this.readJson(await this.send('POST', path, body), parse);
  }

  async put<T>(path: string, body: unknown, parse: Parser<T>): Promise<T> {
    return this.readJson(await this.send('PUT', path, body), parse);
  }

  async patch<T>(path: string, body: unknown, parse: Parser<T>): Promise<T> {
    return this.readJson(await this.send('PATCH', path, body), parse);
  }

  async delete(path: string): Promise<void> {
    const response = await this.send('DELETE', path);
    if (response.status === 202) {
      throw new ApiError(
        202,
        'deletion_pending',
        'La eliminación todavía está pendiente de completarse',
      );
    }
  }

  /** Usa XMLHttpRequest porque fetch no informa cuánto se ha enviado. */
  async upload<T>(
    path: string,
    file: Blob,
    parse: Parser<T>,
    { onProgress, signal }: UploadOptions,
  ): Promise<T> {
    const token = this.auth.getToken();
    const response = await new Promise<Response>((resolve, reject) => {
      const request = new XMLHttpRequest();
      request.open('POST', `${this.baseUrl}${path}`);
      request.setRequestHeader('Content-Type', UPLOAD_CONTENT_TYPE);
      if (token !== null) {
        request.setRequestHeader('Authorization', `Bearer ${token}`);
      }
      request.upload.addEventListener('progress', (event) => {
        if (event.lengthComputable) {
          onProgress(event.loaded / event.total);
        }
      });
      request.addEventListener('load', () => {
        resolve(new Response(request.responseText, { status: request.status }));
      });
      request.addEventListener('error', () => {
        reject(networkError());
      });
      request.addEventListener('abort', () => {
        reject(new ApiError(NETWORK_ERROR_STATUS, UPLOAD_CANCELLED, 'Subida cancelada'));
      });
      signal.addEventListener(
        'abort',
        () => {
          request.abort();
        },
        { once: true },
      );
      request.send(file);
    });
    return this.readJson(await this.requireSuccess(response, token), parse);
  }

  private async send(method: string, path: string, body?: unknown): Promise<Response> {
    const token = this.auth.getToken();
    const headers: Record<string, string> = {};
    if (token !== null) {
      headers.Authorization = `Bearer ${token}`;
    }
    if (body !== undefined) {
      headers['Content-Type'] = 'application/json';
    }

    const response = await this.fetchOrThrow(`${this.baseUrl}${path}`, {
      method,
      headers,
      body: body === undefined ? undefined : JSON.stringify(body),
    });
    return this.requireSuccess(response, token);
  }

  private async requireSuccess(response: Response, token: string | null): Promise<Response> {
    if (response.ok) {
      return response;
    }
    // Sin token, un 401 es una clave incorrecta en el login, no una sesión vencida.
    if (response.status === HTTP_UNAUTHORIZED && token !== null) {
      this.auth.onUnauthorized();
    }
    throw await toApiError(response);
  }

  private async fetchOrThrow(url: string, init: RequestInit): Promise<Response> {
    try {
      return await fetch(url, init);
    } catch {
      // fetch solo rechaza si no hubo respuesta, y el motivo no se expone por seguridad.
      throw networkError();
    }
  }

  private async readJson<T>(response: Response, parse: Parser<T>): Promise<T> {
    try {
      return parse(await response.json());
    } catch (cause) {
      const detail = cause instanceof Error ? cause.message : 'respuesta ilegible';
      throw new ApiError(response.status, 'invalid_response', `Respuesta inesperada: ${detail}`);
    }
  }
}
