/** Status de un error que nunca llegó a ser respuesta HTTP (servidor caído, CORS). */
export const NETWORK_ERROR_STATUS = 0;

/** Error común de las APIs (SPEC 4.1): `code` es estable y sirve para decidir qué mostrar. */
export class ApiError extends Error {
  readonly status: number;
  readonly code: string;

  constructor(status: number, code: string, message: string) {
    super(message);
    this.name = 'ApiError';
    this.status = status;
    this.code = code;
  }
}
