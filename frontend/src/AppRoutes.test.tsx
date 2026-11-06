import { render, screen } from '@testing-library/react';
import { MemoryRouter } from 'react-router';
import { describe, expect, it } from 'vitest';
import { AppRoutes } from './AppRoutes';

function renderAt(path: string) {
  render(
    <MemoryRouter initialEntries={[path]}>
      <AppRoutes />
    </MemoryRouter>,
  );
}

describe('AppRoutes', () => {
  it.each([
    ['/', 'Iniciar sesión'],
    ['/login', 'Iniciar sesión'],
    ['/register', 'Crear cuenta'],
    ['/videos', 'Videos'],
    ['/videos/abc', 'Reproducir video'],
    ['/upload', 'Subir video'],
    ['/ruta-que-no-existe', 'Iniciar sesión'],
  ])('en %s muestra "%s"', (path, heading) => {
    renderAt(path);

    expect(screen.getByRole('heading', { name: heading })).toBeInTheDocument();
  });
});
