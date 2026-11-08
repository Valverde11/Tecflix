import { render, screen } from '@testing-library/react';
import userEvent from '@testing-library/user-event';
import { delay, http, HttpResponse } from 'msw';
import { MemoryRouter } from 'react-router';
import { describe, expect, it } from 'vitest';
import { createServices } from '../../api/createServices';
import { AppProviders } from '../../AppProviders';
import { AppRoutes } from '../../AppRoutes';
import { TEST_API_CONFIG, TEST_PASSWORD, testSession } from '../../test/fixtures';
import { renderApp } from '../../test/renderApp';
import { server } from '../../test/server';

const LOGIN_URL = `${TEST_API_CONFIG.usersApiUrl}/api/auth/login`;

async function fillAndSubmit(username: string, password: string) {
  await userEvent.type(screen.getByLabelText('Nombre de usuario'), username);
  await userEvent.type(screen.getByLabelText('Contraseña'), password);
  await userEvent.click(screen.getByRole('button', { name: 'Iniciar sesión' }));
}

describe('LoginPage', () => {
  it('entra a los videos y guarda la sesión', async () => {
    const services = renderApp('/login');

    await fillAndSubmit('ana', TEST_PASSWORD);

    expect(await screen.findByRole('heading', { name: 'Videos' })).toBeInTheDocument();
    expect(services.sessionStore.getSession()).toEqual(testSession);
  });

  it('vuelve a la página que se había pedido', async () => {
    renderApp('/upload');

    await fillAndSubmit('ana', TEST_PASSWORD);

    expect(await screen.findByRole('heading', { name: 'Subir video' })).toBeInTheDocument();
  });

  it('ignora un destino que no es texto', async () => {
    window.sessionStorage.clear();
    render(
      <MemoryRouter initialEntries={[{ pathname: '/login', state: { from: 42 } }]}>
        <AppProviders services={createServices(TEST_API_CONFIG, window.sessionStorage)}>
          <AppRoutes />
        </AppProviders>
      </MemoryRouter>,
    );

    await fillAndSubmit('ana', TEST_PASSWORD);

    expect(await screen.findByRole('heading', { name: 'Videos' })).toBeInTheDocument();
  });

  it('avisa si la contraseña no coincide y sigue en el formulario', async () => {
    const services = renderApp('/login');

    await fillAndSubmit('ana', 'incorrecta');

    expect(await screen.findByRole('alert')).toHaveTextContent(
      'El usuario o la contraseña no coinciden.',
    );
    expect(services.sessionStore.getSession()).toBeNull();
  });

  it('pide los campos vacíos sin llamar a la API', async () => {
    renderApp('/login');

    await userEvent.click(screen.getByRole('button', { name: 'Iniciar sesión' }));

    expect(screen.getByLabelText('Nombre de usuario')).toHaveAccessibleDescription(
      'Escribe tu nombre de usuario.',
    );
    expect(screen.getByLabelText('Contraseña')).toHaveAccessibleDescription(
      'Escribe tu contraseña.',
    );
    expect(screen.queryByRole('alert')).not.toBeInTheDocument();
  });

  it('quita el error del campo al corregirlo', async () => {
    renderApp('/login');
    await userEvent.click(screen.getByRole('button', { name: 'Iniciar sesión' }));

    await userEvent.type(screen.getByLabelText('Nombre de usuario'), 'a');

    expect(screen.getByLabelText('Nombre de usuario')).toHaveAttribute('aria-invalid', 'false');
    expect(screen.getByLabelText('Contraseña')).toHaveAttribute('aria-invalid', 'true');
  });

  it('explica que el servidor no responde', async () => {
    server.use(http.post(LOGIN_URL, () => HttpResponse.error()));
    renderApp('/login');

    await fillAndSubmit('ana', TEST_PASSWORD);

    expect(await screen.findByRole('alert')).toHaveTextContent('No se pudo conectar');
  });

  it('muestra el mensaje de la API para errores no previstos', async () => {
    server.use(
      http.post(LOGIN_URL, () =>
        HttpResponse.json({ error: 'internal_error', message: 'Falló algo' }, { status: 500 }),
      ),
    );
    renderApp('/login');

    await fillAndSubmit('ana', TEST_PASSWORD);

    expect(await screen.findByRole('alert')).toHaveTextContent('Falló algo');
  });

  it('bloquea el botón mientras espera, para no enviar dos veces', async () => {
    server.use(
      http.post(LOGIN_URL, async () => {
        await delay('infinite');
        return HttpResponse.json(testSession);
      }),
    );
    renderApp('/login');

    await fillAndSubmit('ana', TEST_PASSWORD);

    expect(await screen.findByRole('button', { name: 'Iniciando sesión…' })).toBeDisabled();
  });

  it('lleva al registro', async () => {
    renderApp('/login');

    await userEvent.click(screen.getByRole('link', { name: 'Crear cuenta' }));

    expect(screen.getByRole('heading', { name: 'Crear cuenta' })).toBeInTheDocument();
  });
});
