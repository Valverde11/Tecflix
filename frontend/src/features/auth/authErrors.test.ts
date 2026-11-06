import { describe, expect, it } from 'vitest';
import { ApiError } from '../../api/ApiError';
import { describeAuthError } from './authErrors';

describe('describeAuthError', () => {
  it('prefiere el texto propio para los códigos conocidos', () => {
    const error = new ApiError(401, 'invalid_credentials', 'Texto de la API');

    expect(describeAuthError(error)).toBe('El usuario o la contraseña no coinciden.');
  });

  it('usa el mensaje de la API para códigos desconocidos', () => {
    expect(describeAuthError(new ApiError(500, 'otro', 'Texto de la API'))).toBe('Texto de la API');
  });

  it('da un mensaje genérico si el error no viene de la API', () => {
    expect(describeAuthError(new Error('fallo interno'))).toBe(
      'Ocurrió un error inesperado. Intenta de nuevo.',
    );
  });
});
