import { screen } from '@testing-library/react';
import userEvent from '@testing-library/user-event';
import { delay, http, HttpResponse } from 'msw';
import { describe, expect, it, vi } from 'vitest';
import { TEST_API_CONFIG, testSession, testVideo } from '../../test/fixtures';
import { renderApp } from '../../test/renderApp';
import { server } from '../../test/server';
import type { Video, VideoStatus } from '../../types/video';

const VIDEO_URL = `${TEST_API_CONFIG.metadataApiUrl}/api/metadata/videos/${testVideo.videoId}`;
const VIDEO_PATH = `/videos/${testVideo.videoId}`;

vi.mock('./VideoPlayer', () => ({
  VideoPlayer: ({ manifestUrl, token }: { manifestUrl: string; token: string | null }) => (
    <p>
      Reproductor {manifestUrl} {token}
    </p>
  ),
}));

function toMetadataJson(video: Video) {
  return {
    id: video.videoId,
    owner_id: String(video.ownerId),
    owner_name: video.author,
    title: video.title,
    description: video.description,
    topic: video.topic,
    tags: video.tags,
    duration_seconds: video.durationSeconds,
    size_bytes: video.sizeBytes,
    visibility: video.visibility,
    uploaded_at: video.uploadedAt,
    updated_at: video.uploadedAt,
    genres: [],
    cast: [],
  };
}

function serveVideo(changes: Partial<Video> = {}) {
  const video = { ...testVideo, ...changes };
  server.use(http.get(VIDEO_URL, () => HttpResponse.json(toMetadataJson(video))));
}

describe('VideoDetailPage', () => {
  it('reproduce el manifiesto de streaming con el token de la sesión', async () => {
    renderApp(VIDEO_PATH, testSession);

    const manifest = `${TEST_API_CONFIG.streamingApiUrl}/api/videos/${testVideo.videoId}/index.m3u8`;
    expect(await screen.findByText(`Reproductor ${manifest} ${testSession.token}`)).toBeVisible();
  });

  it('muestra la metadata del video', async () => {
    renderApp(VIDEO_PATH, testSession);

    expect(await screen.findByRole('heading', { name: testVideo.title })).toBeInTheDocument();
    expect(
      screen.getByText(/Estructuras de datos, por ana, el .*2026.*Público/),
    ).toBeInTheDocument();
    expect(screen.getByText(testVideo.description)).toBeInTheDocument();
    expect(screen.getByRole('list', { name: 'Etiquetas' })).toHaveTextContent('btree');
  });

  it('indica que un video es privado', async () => {
    serveVideo({ visibility: 'private' });
    renderApp(VIDEO_PATH, testSession);

    expect(await screen.findByText(/Privado/)).toBeInTheDocument();
  });

  it('omite descripción y etiquetas si están vacías', async () => {
    serveVideo({ description: '', tags: [] });
    renderApp(VIDEO_PATH, testSession);

    await screen.findByRole('heading', { name: testVideo.title });

    expect(screen.queryByRole('list', { name: 'Etiquetas' })).not.toBeInTheDocument();
  });

  describe('transcodificación', () => {
    const STATUS_URL = `${TEST_API_CONFIG.streamingApiUrl}/api/videos/${testVideo.videoId}`;

    function serveStatus(...statuses: readonly VideoStatus[]) {
      let call = 0;
      server.use(
        http.get(STATUS_URL, () => {
          const status = statuses[Math.min(call, statuses.length - 1)];
          call += 1;
          return HttpResponse.json({ status });
        }),
      );
      return () => call;
    }

    it.each([
      ['queued', 'está en cola'],
      ['processing', 'se está procesando'],
      ['failed', 'No se pudo procesar'],
    ] as const)('con estado %s no carga el reproductor', async (status, text) => {
      serveVideo();
      serveStatus(status);
      renderApp(VIDEO_PATH, testSession);

      expect(await screen.findByText(new RegExp(text))).toBeInTheDocument();
      expect(screen.queryByText(/^Reproductor/)).not.toBeInTheDocument();
    });

    it('consulta streaming porque es la única fuente de verdad del estado', async () => {
      const calls = serveStatus('ready');
      renderApp(VIDEO_PATH, testSession);

      await screen.findByText(/^Reproductor/);

      expect(calls()).toBe(1);
    });

    it('consulta periódicamente hasta que streaming termina la transcodificación', async () => {
      const calls = serveStatus('processing', 'ready');
      renderApp(VIDEO_PATH, testSession);

      expect(await screen.findByText(/se está procesando/)).toBeInTheDocument();
      expect(await screen.findByText(/^Reproductor/)).toBeInTheDocument();

      expect(calls()).toBe(2);
    });

    it('muestra un error recuperable si streaming no puede informar el estado', async () => {
      serveVideo();
      server.use(http.get(STATUS_URL, () => HttpResponse.error()));
      renderApp(VIDEO_PATH, testSession);

      expect(await screen.findByRole('alert')).toHaveTextContent(
        'No se pudo consultar el estado del video.',
      );
    });
  });

  it('avisa mientras carga', async () => {
    server.use(
      http.get(VIDEO_URL, async () => {
        await delay('infinite');
        return HttpResponse.json(toMetadataJson(testVideo));
      }),
    );
    renderApp(VIDEO_PATH, testSession);

    expect(await screen.findByRole('status')).toHaveTextContent('Cargando video…');
  });

  it.each([404, 403])('trata igual un %d, sin revelar si el video existe', async (status) => {
    server.use(http.get(VIDEO_URL, () => HttpResponse.json({ error: 'x' }, { status })));
    renderApp(VIDEO_PATH, testSession);

    expect(
      await screen.findByRole('heading', { name: 'Este video no existe o no tienes acceso a él' }),
    ).toBeInTheDocument();
  });

  it('explica otros errores y permite reintentar', async () => {
    let attempts = 0;
    server.use(
      http.get(VIDEO_URL, () => {
        attempts += 1;
        return attempts === 1
          ? HttpResponse.json({ error: 'x', message: 'Falló Mongo' }, { status: 503 })
          : HttpResponse.json(toMetadataJson(testVideo));
      }),
    );
    renderApp(VIDEO_PATH, testSession);

    expect(await screen.findByRole('alert')).toHaveTextContent('Falló Mongo');

    await userEvent.click(screen.getByRole('button', { name: 'Intentar de nuevo' }));

    expect(await screen.findByRole('heading', { name: testVideo.title })).toBeInTheDocument();
  });

  it('vuelve a la lista', async () => {
    renderApp(VIDEO_PATH, testSession);

    await userEvent.click(screen.getByRole('link', { name: 'Volver a los videos' }));

    expect(screen.getByRole('heading', { name: 'Videos' })).toBeInTheDocument();
  });
});
