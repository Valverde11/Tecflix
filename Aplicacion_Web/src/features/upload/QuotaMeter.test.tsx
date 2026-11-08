import { screen } from '@testing-library/react';
import { http, HttpResponse } from 'msw';
import { describe, expect, it } from 'vitest';
import { TEST_API_CONFIG, testSession, testUser } from '../../test/fixtures';
import { renderApp } from '../../test/renderApp';
import { server } from '../../test/server';
import { formatBytes } from './formatBytes';

const QUOTA_URL = `${TEST_API_CONFIG.usersApiUrl}/api/users/${String(testUser.id)}/quota`;
const ONE_GB = 1_000_000_000;

function serveQuota(usedBytes: number) {
  server.use(http.get(QUOTA_URL, () => HttpResponse.json({ quotaBytes: ONE_GB, usedBytes })));
}

describe('formatBytes', () => {
  it.each([
    [0, '0 B'],
    [999, '999 B'],
    [1000, '1 KB'],
    [1_500_000, '1,5 MB'],
    [ONE_GB, '1 GB'],
    [5_000_000_000_000_000, '5 000 TB'],
  ])('%d bytes se muestran como %s', (bytes, text) => {
    expect(formatBytes(bytes).replace(/\s/g, ' ')).toBe(text);
  });
});

describe('QuotaMeter', () => {
  it('muestra el espacio usado según la API, no según la sesión', async () => {
    serveQuota(250_000_000);
    renderApp('/upload', testSession);

    expect(await screen.findByText('250 MB de 1 GB usados')).toBeInTheDocument();
    expect(screen.getByRole('meter', { name: 'Espacio usado' })).toHaveAttribute(
      'aria-valuenow',
      '25',
    );
  });

  it('aparece también en la lista de videos', async () => {
    serveQuota(0);
    renderApp('/videos', testSession);

    expect(await screen.findByRole('meter', { name: 'Espacio usado' })).toBeInTheDocument();
  });

  it('no se pasa del 100 % si el uso supera la cuota', async () => {
    serveQuota(2 * ONE_GB);
    renderApp('/upload', testSession);

    expect(await screen.findByRole('meter')).toHaveAttribute('aria-valuenow', '100');
  });

  it('avisa si no pudo consultar el espacio', async () => {
    server.use(http.get(QUOTA_URL, () => HttpResponse.json({ error: 'x' }, { status: 500 })));
    renderApp('/upload', testSession);

    expect(await screen.findByText('No se pudo consultar tu espacio.')).toBeInTheDocument();
  });
});
