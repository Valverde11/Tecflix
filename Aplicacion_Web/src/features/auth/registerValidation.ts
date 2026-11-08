import type { FieldErrors } from '../../hooks/useForm';

// Mismas reglas que UserValidator en user-service: la API decide, esto solo evita un viaje
// al servidor y permite explicar el problema junto al campo.
const USERNAME_PATTERN = /^[A-Za-z0-9_.-]{3,32}$/;
// ASCII visible sin "@" a cada lado de la única arroba, con un punto en el dominio.
const EMAIL_PATTERN = /^[!-?A-~]+@[!-?A-~]+\.[!-?A-~]+$/;
const MAX_EMAIL_LENGTH = 64;
const MIN_PASSWORD_BYTES = 8;
const MAX_PASSWORD_BYTES = 128;

export const USERNAME_RULE = 'De 3 a 32 letras sin tilde, números, punto, guion o guion bajo.';
export const PASSWORD_RULE = 'De 8 a 128 caracteres.';

export interface RegisterValues {
  readonly username: string;
  readonly email: string;
  readonly password: string;
  readonly passwordConfirmation: string;
}

export type RegisterField = keyof RegisterValues;

function isValidEmail(email: string): boolean {
  return email.length <= MAX_EMAIL_LENGTH && EMAIL_PATTERN.test(email);
}

function isValidPassword(password: string): boolean {
  // El servidor cuenta bytes de UTF-8: "ñ" ocupa 2, aunque en JavaScript su largo sea 1.
  const bytes = new TextEncoder().encode(password).length;
  return bytes >= MIN_PASSWORD_BYTES && bytes <= MAX_PASSWORD_BYTES;
}

export function findRegisterErrors(values: RegisterValues): FieldErrors<RegisterField> {
  const { username, email, password, passwordConfirmation } = values;
  return {
    ...(!USERNAME_PATTERN.test(username) && { username: USERNAME_RULE }),
    ...(!isValidEmail(email) && { email: 'Escribe un correo como ana@tec.cr.' }),
    ...(!isValidPassword(password) && { password: PASSWORD_RULE }),
    ...(password !== passwordConfirmation && {
      passwordConfirmation: 'Las contraseñas no coinciden.',
    }),
  };
}
