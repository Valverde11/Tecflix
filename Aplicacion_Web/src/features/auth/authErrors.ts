import { ApiError } from '../../api/ApiError';

const UNEXPECTED_ERROR = 'Ocurrió un error inesperado. Intenta de nuevo.';

const MESSAGES_BY_CODE = new Map([
  ['invalid_credentials', 'El usuario o la contraseña no coinciden.'],
  ['network_error', 'No se pudo conectar con el servidor. Intenta de nuevo en un momento.'],
  ['username_taken', 'Ese nombre de usuario ya existe. Prueba con otro.'],
  ['email_taken', 'Ese correo ya tiene una cuenta. Inicia sesión con ella.'],
]);

export function describeAuthError(error: unknown): string {
  if (error instanceof ApiError) {
    return MESSAGES_BY_CODE.get(error.code) ?? error.message;
  }
  return UNEXPECTED_ERROR;
}
