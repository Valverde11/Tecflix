import { screen, waitFor, within } from '@testing-library/react';
import userEvent from '@testing-library/user-event';
import { delay, http, HttpResponse } from 'msw';
import { describe, expect, it } from 'vitest';
import { TEST_API_CONFIG, testSession, testVideo } from '../../test/fixtures';
import { renderApp } from '../../test/renderApp';
import { server } from '../../test/server';
import { queryCatalog, videoCatalog } from '../../test/videoCatalog';
import type { SortOrder, VideoSortField } from '../../types/video';

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

  describe('estados', () => {
    const emptyPage = { items: [], page: 1, pageSize: 10, total: 0 };

    it('avisa mientras carga', async () => {
      server.use(
        http.get(LIST_URL, async () => {
          await delay('infinite');
          return HttpResponse.json(emptyPage);
        }),
      );
      renderApp('/videos', testSession);

      expect(await screen.findByRole('status')).toHaveTextContent('Cargando videos…');
    });

    it('invita a subir el primer video si no hay ninguno', async () => {
      server.use(http.get(LIST_URL, () => HttpResponse.json(emptyPage)));
      renderApp('/videos', testSession);

      expect(await screen.findByText('Todavía no hay videos.')).toBeInTheDocument();
      expect(screen.getByRole('link', { name: 'Subir el primero' })).toHaveAttribute(
        'href',
        '/upload',
      );
    });

    it('distingue una página fuera de rango y lleva a la primera con el mismo orden', async () => {
      server.use(
        http.get(LIST_URL, ({ request }) => {
          const page = Number(new URL(request.url).searchParams.get('page'));
          const items = page === 1 ? [testVideo] : [];
          return HttpResponse.json({ items, page, pageSize: 10, total: 1 });
        }),
      );
      renderApp('/videos?page=9&sort=title-asc', testSession);

      await userEvent.click(await screen.findByRole('link', { name: 'Ir a la primera página' }));

      expect(await shownTitles()).toEqual([testVideo.title]);
      expect(screen.getByLabelText('Ordenar por')).toHaveValue('title-asc');
    });

    it('explica el error y permite reintentar', async () => {
      let attempts = 0;
      server.use(
        http.get(LIST_URL, () => {
          attempts += 1;
          return attempts === 1
            ? HttpResponse.json({ error: 'internal', message: 'Falló Mongo' }, { status: 503 })
            : HttpResponse.json({ items: [testVideo], page: 1, pageSize: 10, total: 1 });
        }),
      );
      renderApp('/videos', testSession);

      expect(await screen.findByRole('alert')).toHaveTextContent('Falló Mongo');

      await userEvent.click(screen.getByRole('button', { name: 'Intentar de nuevo' }));

      expect(await shownTitles()).toEqual([testVideo.title]);
      expect(screen.queryByRole('alert')).not.toBeInTheDocument();
    });

    it('señala una respuesta que no cumple el contrato', async () => {
      server.use(http.get(LIST_URL, () => HttpResponse.json({ videos: [] })));
      renderApp('/videos', testSession);

      expect(await screen.findByRole('alert')).toHaveTextContent('"items"');
    });

    it('vuelve al login si metadata rechaza el token', async () => {
      server.use(
        http.get(LIST_URL, () => HttpResponse.json({ error: 'unauthorized' }, { status: 401 })),
      );
      const services = renderApp('/videos', testSession);

      expect(await screen.findByRole('heading', { name: 'Iniciar sesión' })).toBeInTheDocument();
      expect(services.sessionStore.getSession()).toBeNull();
    });

    it('mantiene visible la página anterior mientras llega la siguiente', async () => {
      server.use(
        http.get(LIST_URL, async ({ request }) => {
          const page = Number(new URL(request.url).searchParams.get('page'));
          if (page === 2) {
            await delay('infinite');
          }
          return HttpResponse.json({ items: [testVideo], page, pageSize: 10, total: 11 });
        }),
      );
      renderApp('/videos', testSession);

      await userEvent.click(await screen.findByRole('link', { name: 'Siguiente' }));

      const list = await screen.findByRole('list', { name: 'Lista de videos', busy: true });
      expect(within(list).getByRole('heading', { name: testVideo.title })).toBeInTheDocument();
    });
  });

  describe('orden y paginación', () => {
    /** Responde como metadata y guarda lo que pidió la web. */
    function trackRequests(): string[] {
      const requests: string[] = [];
      server.use(
        http.get(LIST_URL, ({ request }) => {
          const url = new URL(request.url);
          requests.push(url.search);
          const query = url.searchParams;
          return HttpResponse.json(
            queryCatalog(videoCatalog, {
              page: Number(query.get('page')),
              pageSize: Number(query.get('pageSize')),
              sortBy: query.get('sortBy') as VideoSortField,
              order: query.get('order') as SortOrder,
            }),
          );
        }),
      );
      return requests;
    }

    it('lee la página y el orden de la URL', async () => {
      const requests = trackRequests();
      renderApp('/videos?page=2&sort=title-asc', testSession);

      expect(await screen.findByText('Página 2 de 3')).toBeInTheDocument();
      expect(requests).toEqual(['?page=2&pageSize=10&sortBy=title&order=asc']);
      expect(screen.getByLabelText('Ordenar por')).toHaveValue('title-asc');
    });

    it.each([
      ['/videos?page=abc'],
      ['/videos?page=0'],
      ['/videos?page=-2'],
      ['/videos?page=1.5'],
      ['/videos?sort=inventado'],
    ])('ignora valores inválidos en %s', async (path) => {
      const requests = trackRequests();
      renderApp(path, testSession);

      await screen.findByText('Página 1 de 3');

      expect(requests).toEqual(['?page=1&pageSize=10&sortBy=uploadedAt&order=desc']);
    });

    it('avanza y retrocede pidiendo cada página a la API', async () => {
      const requests = trackRequests();
      renderApp('/videos', testSession);

      await userEvent.click(await screen.findByRole('link', { name: 'Siguiente' }));
      expect(await screen.findByText('Página 2 de 3')).toBeInTheDocument();

      await userEvent.click(screen.getByRole('link', { name: 'Anterior' }));
      expect(await screen.findByText('Página 1 de 3')).toBeInTheDocument();

      expect(requests[1]).toContain('page=2');
    });

    it('no ofrece ir antes de la primera ni después de la última', async () => {
      trackRequests();
      renderApp('/videos', testSession);

      await screen.findByText('Página 1 de 3');
      expect(screen.queryByRole('link', { name: 'Anterior' })).not.toBeInTheDocument();

      await userEvent.click(screen.getByRole('link', { name: 'Siguiente' }));
      await userEvent.click(await screen.findByRole('link', { name: 'Siguiente' }));

      await screen.findByText('Página 3 de 3');
      expect(screen.queryByRole('link', { name: 'Siguiente' })).not.toBeInTheDocument();
    });

    it('conserva el orden al cambiar de página', async () => {
      const requests = trackRequests();
      renderApp('/videos?sort=author-desc', testSession);

      await userEvent.click(await screen.findByRole('link', { name: 'Siguiente' }));

      await waitFor(() => {
        expect(requests.at(-1)).toBe('?page=2&pageSize=10&sortBy=author&order=desc');
      });
    });

    it('al cambiar el orden lo pide a la API y vuelve a la primera página', async () => {
      const requests = trackRequests();
      renderApp('/videos?page=3', testSession);
      await screen.findByText('Página 3 de 3');

      await userEvent.selectOptions(screen.getByLabelText('Ordenar por'), 'title-desc');

      await waitFor(() => {
        expect(requests.at(-1)).toBe('?page=1&pageSize=10&sortBy=title&order=desc');
      });
    });

    it('no muestra paginación si todo cabe en una página', async () => {
      server.use(
        http.get(LIST_URL, () =>
          HttpResponse.json({ items: [testVideo], page: 1, pageSize: 10, total: 1 }),
        ),
      );
      renderApp('/videos', testSession);

      await shownTitles();

      expect(screen.queryByRole('navigation', { name: 'Páginas de videos' })).toBeNull();
    });
  });
});
