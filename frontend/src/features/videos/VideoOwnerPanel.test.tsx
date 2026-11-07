import { screen, waitFor } from '@testing-library/react';
import userEvent from '@testing-library/user-event';
import { http, HttpResponse } from 'msw';
import { describe, expect, it, vi } from 'vitest';
import { TEST_API_CONFIG, testSession, testUser, testVideo } from '../../test/fixtures';
import { videoStore } from '../../test/handlers';
import { renderApp } from '../../test/renderApp';
import { server } from '../../test/server';

const VIDEO_URL = `${TEST_API_CONFIG.metadataApiUrl}/api/metadata/videos/${testVideo.videoId}`;
const VIDEO_PATH = `/videos/${testVideo.videoId}`;

vi.mock('../player/VideoPlayer', () => ({ VideoPlayer: () => <p>Reproductor</p> }));

async function renderDetail() {
  const services = renderApp(VIDEO_PATH, testSession);
  await screen.findByRole('heading', { name: testVideo.title });
  return services;
}

function clickButton(name: string) {
  return userEvent.click(screen.getByRole('button', { name }));
}

describe('VideoOwnerPanel', () => {
  it('no aparece en videos ajenos', async () => {
    videoStore.update(testVideo.videoId, { ownerId: testUser.id + 1 });

    await renderDetail();

    expect(screen.queryByRole('button', { name: 'Editar datos' })).not.toBeInTheDocument();
  });

  describe('editar', () => {
    it('carga los datos actuales', async () => {
      await renderDetail();

      await clickButton('Editar datos');

      expect(screen.getByLabelText('Título')).toHaveValue(testVideo.title);
      expect(screen.getByLabelText('Tema')).toHaveValue(testVideo.topic);
      expect(screen.getByLabelText('Descripción')).toHaveValue(testVideo.description);
      expect(screen.getByRole('textbox', { name: 'Etiquetas' })).toHaveValue('btree');
    });

    it('guarda los cambios y los muestra sin recargar', async () => {
      await renderDetail();
      await clickButton('Editar datos');
      await userEvent.clear(screen.getByLabelText('Título'));
      await userEvent.type(screen.getByLabelText('Título'), 'Título nuevo');
      await userEvent.clear(screen.getByRole('textbox', { name: 'Etiquetas' }));
      await userEvent.type(screen.getByRole('textbox', { name: 'Etiquetas' }), 'uno, dos');

      await clickButton('Guardar cambios');

      expect(await screen.findByRole('heading', { name: 'Título nuevo' })).toBeInTheDocument();
      expect(screen.queryByLabelText('Título')).not.toBeInTheDocument();
      expect(videoStore.find(testVideo.videoId)).toMatchObject({
        title: 'Título nuevo',
        tags: ['uno', 'dos'],
        visibility: testVideo.visibility,
      });
    });

    it('la lista vuelve a pedirse tras editar', async () => {
      const services = await renderDetail();
      const listKey = ['videos', { page: 1 }];
      services.queryClient.setQueryData(listKey, { items: [] });
      await clickButton('Editar datos');

      await clickButton('Guardar cambios');

      await waitFor(() => {
        expect(services.queryClient.getQueryState(listKey)?.isInvalidated).toBe(true);
      });
    });

    it('valida antes de enviar', async () => {
      await renderDetail();
      await clickButton('Editar datos');
      await userEvent.clear(screen.getByLabelText('Título'));

      await clickButton('Guardar cambios');

      expect(screen.getByLabelText('Título')).toHaveAccessibleDescription('Escribe un título.');
      expect(videoStore.find(testVideo.videoId)?.title).toBe(testVideo.title);
    });

    it('muestra el error de la descripción', async () => {
      await renderDetail();
      await clickButton('Editar datos');
      const description = screen.getByLabelText('Descripción');
      await userEvent.clear(description);
      await userEvent.click(description);
      await userEvent.paste('a'.repeat(5001));

      await clickButton('Guardar cambios');

      expect(description).toHaveAccessibleDescription(/5000/);
    });

    it('cancelar descarta lo escrito', async () => {
      await renderDetail();
      await clickButton('Editar datos');
      await userEvent.type(screen.getByLabelText('Título'), ' cambiado');

      await clickButton('Cancelar');
      await clickButton('Editar datos');

      expect(screen.getByLabelText('Título')).toHaveValue(testVideo.title);
    });

    it('explica si no se pudo guardar y conserva lo escrito', async () => {
      server.use(
        http.put(VIDEO_URL, () =>
          HttpResponse.json({ error: 'forbidden', message: 'No es tu video' }, { status: 403 }),
        ),
      );
      await renderDetail();
      await clickButton('Editar datos');
      await userEvent.type(screen.getByLabelText('Título'), '!');

      await clickButton('Guardar cambios');

      expect(await screen.findByRole('alert')).toHaveTextContent('No es tu video');
      expect(screen.getByLabelText('Título')).toHaveValue(`${testVideo.title}!`);
    });
  });

  describe('cambiar acceso', () => {
    it('hace privado un video público y lo muestra', async () => {
      await renderDetail();

      await clickButton('Hacer privado');

      expect(await screen.findByRole('button', { name: 'Hacer público' })).toBeInTheDocument();
      expect(screen.getByText(/Privado/)).toBeInTheDocument();
      expect(videoStore.find(testVideo.videoId)?.visibility).toBe('private');
    });

    it('hace público un video privado', async () => {
      videoStore.update(testVideo.videoId, { visibility: 'private' });
      await renderDetail();

      await clickButton('Hacer público');

      expect(await screen.findByRole('button', { name: 'Hacer privado' })).toBeInTheDocument();
      expect(videoStore.find(testVideo.videoId)?.visibility).toBe('public');
    });

    it('conserva los demás datos', async () => {
      await renderDetail();

      await clickButton('Hacer privado');

      await screen.findByRole('button', { name: 'Hacer público' });
      expect(videoStore.find(testVideo.videoId)).toMatchObject({
        title: testVideo.title,
        description: testVideo.description,
        topic: testVideo.topic,
        tags: testVideo.tags,
      });
    });

    it('explica si no se pudo cambiar y no aparenta el cambio', async () => {
      server.use(
        http.put(VIDEO_URL, () =>
          HttpResponse.json({ error: 'x', message: 'Falló Mongo' }, { status: 503 }),
        ),
      );
      await renderDetail();

      await clickButton('Hacer privado');

      expect(await screen.findByRole('alert')).toHaveTextContent('Falló Mongo');
      expect(screen.getByRole('button', { name: 'Hacer privado' })).toBeInTheDocument();
    });
  });
});
