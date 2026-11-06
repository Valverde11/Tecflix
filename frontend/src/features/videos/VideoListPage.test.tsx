import { screen, within } from '@testing-library/react';
import { http, HttpResponse } from 'msw';
import { describe, expect, it } from 'vitest';
import { TEST_API_CONFIG, testSession, testVideo } from '../../test/fixtures';
import { renderApp } from '../../test/renderApp';
import { server } from '../../test/server';

const LIST_URL = `${TEST_API_CONFIG.metadataApiUrl}/api/metadata/videos`;

async function shownTitles() {
  const list = await screen.findByRole('list', { name: 'Lista de videos' });
  const headings = await within(list).findAllByRole('heading');
  return headings.map((heading) => heading.textContent);
}

describe('VideoListPage', () => {
  it('pide a metadata la primera página, con los más recientes primero', async () => {
    let requested = '';
    server.use(
      http.get(LIST_URL, ({ request }) => {
        requested = new URL(request.url).search;
        return HttpResponse.json({ items: [testVideo], page: 1, pageSize: 10, total: 1 });
      }),
    );
    renderApp('/videos', testSession);

    await shownTitles();

    expect(requested).toBe('?page=1&pageSize=10&sortBy=uploadedAt&order=desc');
  });

  it('respeta el orden de la API y no reordena por su cuenta', async () => {
    const items = ['Zeta', 'Alfa', 'Medio'].map((title, index) => ({
      ...testVideo,
      title,
      videoId: String(index).repeat(32),
    }));
    server.use(
      http.get(LIST_URL, () => HttpResponse.json({ items, page: 1, pageSize: 10, total: 3 })),
    );
    renderApp('/videos', testSession);

    const titles = await shownTitles();

    expect(titles).toEqual(['Zeta', 'Alfa', 'Medio']);
  });
});
