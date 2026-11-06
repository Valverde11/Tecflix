import { act, render, screen } from '@testing-library/react';
import userEvent from '@testing-library/user-event';
import { describe, expect, it } from 'vitest';
import { testSession } from './test/fixtures';
import { renderApp } from './test/renderApp';
import { useServices } from './hooks/useServices';

function expectHeading(name: string) {
  expect(screen.getByRole('heading', { name })).toBeInTheDocument();
}

describe('rutas sin sesión', () => {
  it.each([
    ['/', 'Iniciar sesión'],
    ['/login', 'Iniciar sesión'],
    ['/register', 'Crear cuenta'],
    ['/ruta-que-no-existe', 'Iniciar sesión'],
  ])('en %s muestra "%s"', (path, heading) => {
    renderApp(path);

    expectHeading(heading);
  });

  it.each(['/videos', '/videos/abc', '/upload'])('%s exige iniciar sesión', (path) => {
    renderApp(path);

    expectHeading('Iniciar sesión');
  });
});

describe('rutas con sesión', () => {
  it.each([
    ['/videos', 'Videos'],
    ['/videos/abc', 'Reproducir video'],
    ['/upload', 'Subir video'],
  ])('en %s muestra "%s"', (path, heading) => {
    renderApp(path, testSession);

    expectHeading(heading);
  });

  it.each(['/', '/login', '/register'])('%s lleva a los videos', (path) => {
    renderApp(path, testSession);

    expectHeading('Videos');
  });

  it('muestra quién inició sesión', () => {
    renderApp('/videos', testSession);

    expect(screen.getByText(testSession.user.username)).toBeInTheDocument();
  });

  it('cerrar sesión vuelve al login y borra la sesión', async () => {
    const services = renderApp('/videos', testSession);

    await userEvent.click(screen.getByRole('button', { name: 'Cerrar sesión' }));

    expectHeading('Iniciar sesión');
    expect(services.sessionStore.getSession()).toBeNull();
  });

  it('una sesión vencida a mitad de uso devuelve al login', () => {
    const services = renderApp('/upload', testSession);

    act(() => {
      services.sessionStore.clear();
    });

    expectHeading('Iniciar sesión');
  });
});

describe('useServices', () => {
  function WithoutProviders() {
    useServices();
    return null;
  }

  it('explica el error si falta AppProviders', () => {
    expect(() => render(<WithoutProviders />)).toThrow('AppProviders');
  });
});
