import { screen } from '@testing-library/react';
import userEvent from '@testing-library/user-event';
import { http, HttpResponse } from 'msw';
import { describe, expect, it } from 'vitest';
import { TAKEN_EMAIL, TAKEN_USERNAME, TEST_API_CONFIG, TEST_PASSWORD } from '../../test/fixtures';
import { renderApp } from '../../test/renderApp';
import { server } from '../../test/server';

const USERS_URL = `${TEST_API_CONFIG.usersApiUrl}/api/users`;

const validForm = { username: 'luis', email: 'luis@tec.cr', password: TEST_PASSWORD };

async function fillAndSubmit(changes: Partial<typeof validForm> = {}) {
  const { username, email, password } = { ...validForm, ...changes };
  await userEvent.type(screen.getByLabelText('Nombre de usuario'), username);
  await userEvent.type(screen.getByLabelText('Correo'), email);
  await userEvent.type(screen.getByLabelText('Contraseña'), password);
  await userEvent.type(screen.getByLabelText('Repite la contraseña'), password);
  await userEvent.click(screen.getByRole('button', { name: 'Crear cuenta' }));
}

describe('RegisterPage', () => {
  it('crea la cuenta y entra sin volver a pedir los datos', async () => {
    const services = renderApp('/register');

    await fillAndSubmit();

    expect(await screen.findByRole('heading', { name: 'Videos' })).toBeInTheDocument();
    expect(services.sessionStore.getSession()?.user.username).toBe('luis');
  });

  it('conserva la página pedida al pasar por el login', async () => {
    renderApp('/upload');
    await userEvent.click(screen.getByRole('link', { name: 'Crear cuenta' }));

    await fillAndSubmit();

    expect(await screen.findByRole('heading', { name: 'Subir video' })).toBeInTheDocument();
  });

  it('no envía la confirmación de contraseña a la API', async () => {
    let received: unknown = null;
    server.use(
      http.post(USERS_URL, async ({ request }) => {
        received = await request.json();
        return HttpResponse.json({ error: 'internal_error' }, { status: 500 });
      }),
    );
    renderApp('/register');

    await fillAndSubmit();
    await screen.findByRole('alert');

    expect(received).toEqual(validForm);
  });

  it('muestra los errores de validación sin llamar a la API', async () => {
    renderApp('/register');

    await userEvent.click(screen.getByRole('button', { name: 'Crear cuenta' }));

    expect(screen.getByLabelText('Nombre de usuario')).toHaveAttribute('aria-invalid', 'true');
    expect(screen.getByLabelText('Correo')).toHaveAccessibleDescription(
      'Escribe un correo como ana@tec.cr.',
    );
    expect(screen.queryByRole('alert')).not.toBeInTheDocument();
  });

  it('marca que las contraseñas no coinciden', async () => {
    renderApp('/register');
    await userEvent.type(screen.getByLabelText('Contraseña'), 'secreta123');
    await userEvent.type(screen.getByLabelText('Repite la contraseña'), 'distinta123');

    await userEvent.click(screen.getByRole('button', { name: 'Crear cuenta' }));

    expect(screen.getByLabelText('Repite la contraseña')).toHaveAccessibleDescription(
      'Las contraseñas no coinciden.',
    );
  });

  it('señala en su campo que el usuario ya existe', async () => {
    renderApp('/register');

    await fillAndSubmit({ username: TAKEN_USERNAME });

    await expect
      .poll(() => screen.getByLabelText('Nombre de usuario').getAttribute('aria-invalid'))
      .toBe('true');
    expect(screen.getByLabelText('Nombre de usuario')).toHaveAccessibleDescription(
      'Ese nombre de usuario ya existe. Prueba con otro.',
    );
    expect(screen.queryByRole('alert')).not.toBeInTheDocument();
  });

  it('señala en su campo que el correo ya existe', async () => {
    renderApp('/register');

    await fillAndSubmit({ email: TAKEN_EMAIL });

    await expect
      .poll(() => screen.getByLabelText('Correo').getAttribute('aria-invalid'))
      .toBe('true');
  });

  it('muestra junto al campo un rechazo que solo detectó el servidor', async () => {
    server.use(
      http.post(USERS_URL, () =>
        HttpResponse.json({ error: 'invalid_email', message: 'Correo rechazado' }, { status: 400 }),
      ),
    );
    renderApp('/register');

    await fillAndSubmit();

    expect(await screen.findByText('Correo rechazado')).toBeInTheDocument();
  });

  it('explica que el servidor no responde', async () => {
    server.use(http.post(USERS_URL, () => HttpResponse.error()));
    renderApp('/register');

    await fillAndSubmit();

    expect(await screen.findByRole('alert')).toHaveTextContent('No se pudo conectar');
  });

  it('lleva al inicio de sesión', async () => {
    renderApp('/register');

    await userEvent.click(screen.getByRole('link', { name: 'Iniciar sesión' }));

    expect(screen.getByRole('heading', { name: 'Iniciar sesión' })).toBeInTheDocument();
  });
});
