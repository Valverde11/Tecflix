import { render, screen } from '@testing-library/react';
import { describe, expect, it } from 'vitest';
import { createServices } from './api/createServices';
import { App } from './App';
import { TEST_API_CONFIG } from './test/fixtures';

describe('App', () => {
  it('arranca en la pantalla de inicio de sesión', () => {
    window.sessionStorage.clear();

    render(<App services={createServices(TEST_API_CONFIG, window.sessionStorage)} />);

    expect(screen.getByRole('heading', { name: 'Iniciar sesión' })).toBeInTheDocument();
  });
});
