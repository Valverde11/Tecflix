import { describe, expect, it } from 'vitest';
import { findRegisterErrors, type RegisterValues } from './registerValidation';

const valid: RegisterValues = {
  username: 'ana.mora-1_x',
  email: 'ana@tec.cr',
  password: 'secreta123',
  passwordConfirmation: 'secreta123',
};

function errorsFor(changes: Partial<RegisterValues>) {
  return findRegisterErrors({ ...valid, ...changes });
}

describe('findRegisterErrors', () => {
  it('acepta datos válidos', () => {
    expect(findRegisterErrors(valid)).toEqual({});
  });

  it.each(['abc', 'a'.repeat(32)])('acepta el usuario en el límite: %s', (username) => {
    expect(errorsFor({ username }).username).toBeUndefined();
  });

  it.each(['', 'ab', 'a'.repeat(33), 'con espacio', 'ñandú'])(
    'rechaza el usuario "%s"',
    (username) => {
      expect(errorsFor({ username }).username).toBeDefined();
    },
  );

  it.each([
    '',
    'sin-arroba.cr',
    '@tec.cr',
    'ana@tec',
    'ana@tec.',
    'ana@.cr',
    'a@b@tec.cr',
    'ana mora@tec.cr',
    'añá@tec.cr',
    `${'a'.repeat(60)}@tec.cr`,
  ])('rechaza el correo "%s"', (email) => {
    expect(errorsFor({ email }).email).toBeDefined();
  });

  it('acepta un correo de exactamente 64 caracteres', () => {
    const email = `${'a'.repeat(57)}@tec.cr`;

    expect(email).toHaveLength(64);
    expect(errorsFor({ email }).email).toBeUndefined();
  });

  it.each([
    ['7 caracteres', '1234567'],
    ['129 caracteres', 'a'.repeat(129)],
    ['65 eñes, que son 130 bytes', 'ñ'.repeat(65)],
  ])('rechaza una contraseña de %s', (_name, password) => {
    expect(errorsFor({ password, passwordConfirmation: password }).password).toBeDefined();
  });

  it.each([
    ['8 bytes', '12345678'],
    ['128 bytes', 'a'.repeat(128)],
    ['4 eñes, que son 8 bytes', 'ññññ'],
  ])('acepta una contraseña de %s', (_name, password) => {
    expect(errorsFor({ password, passwordConfirmation: password }).password).toBeUndefined();
  });

  it('exige repetir la misma contraseña', () => {
    expect(errorsFor({ passwordConfirmation: 'otra-clave' }).passwordConfirmation).toBeDefined();
  });
});
