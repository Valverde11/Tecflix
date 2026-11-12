import { cleanup, screen, waitFor } from '@testing-library/react';
import userEvent from '@testing-library/user-event';
import { delay, http, HttpResponse } from 'msw';
import { beforeEach, describe, expect, it, vi } from 'vitest';
import { TEST_API_CONFIG, testSession, testUser, testVideo, UPLOADED_VIDEO_ID } from '../../test/fixtures';
import { CORS_HEADERS, toMetadataJson, videoStore } from '../../test/handlers';
import { renderApp } from '../../test/renderApp';
import { server } from '../../test/server';

const { usersApiUrl, metadataApiUrl, streamingApiUrl } = TEST_API_CONFIG;
const QUOTA_URL = `${usersApiUrl}/api/users/${String(testUser.id)}/quota`;
const UPLOAD_URL = `${streamingApiUrl}/api/videos`;
const METADATA_URL = `${metadataApiUrl}/api/metadata/videos`;
const DURATION_SECONDS = 90;

vi.mock('./readVideoDuration', () => ({
  readVideoDuration: () => Promise.resolve(90),
}));

vi.mock('../player/VideoPlayer', () => ({ VideoPlayer: () => <p>Reproductor</p> }));

const video = new File(['contenido'], 'Clase de árboles.mp4', { type: 'video/mp4' });

function serveQuota(quotaBytes: number, usedBytes: number) {
  server.use(http.get(QUOTA_URL, () => HttpResponse.json({ quotaBytes, usedBytes })));
}

async function renderUploadPage() {
  renderApp('/upload', testSession);
  await screen.findByRole('meter');
}

async function chooseFile(file = video) {
  await userEvent.upload(screen.getByLabelText('Archivo de video'), file);
}

function clickUpload() {
  return userEvent.click(screen.getByRole('button', { name: 'Subir video' }));
}

beforeEach(() => {
  window.localStorage.clear();
  serveQuota(1000, 0);
});

describe('UploadPage', () => {
  it('sube a streaming, crea la metadata y abre el video', async () => {
    let contentType: string | null = null;
    server.use(
      http.post(UPLOAD_URL, ({ request }) => {
        contentType = request.headers.get('Content-Type');
        return HttpResponse.json(
          { id: UPLOADED_VIDEO_ID, status: 'queued', size_bytes: video.size },
          { status: 202, headers: CORS_HEADERS },
        );
      }),
    );
    await renderUploadPage();
    await chooseFile();
    await userEvent.type(screen.getByLabelText('Tema'), 'Datos');
    await userEvent.type(screen.getByLabelText('Etiquetas'), 'btree, clase');
    await userEvent.click(screen.getByRole('radio', { name: 'Todos' }));

    await clickUpload();

    expect(await screen.findByRole('link', { name: 'Volver a los videos' })).toBeInTheDocument();
    expect(contentType).toBe('application/octet-stream');
    expect(videoStore.find(UPLOADED_VIDEO_ID)).toMatchObject({
      title: 'Clase de árboles',
      topic: 'Datos',
      tags: ['btree', 'clase'],
      visibility: 'public',
      sizeBytes: video.size,
      durationSeconds: DURATION_SECONDS,
    });
  });

  it('usa size_bytes medidos por streaming para crear metadata', async () => {
    const measuredBytes = video.size + 135;
    server.use(
      http.post(UPLOAD_URL, () =>
        HttpResponse.json(
          { id: UPLOADED_VIDEO_ID, status: 'queued', size_bytes: measuredBytes },
          { status: 202, headers: CORS_HEADERS },
        ),
      ),
    );
    await renderUploadPage();
    await chooseFile();
    await clickUpload();

    expect(await screen.findByRole('link', { name: 'Volver a los videos' })).toBeInTheDocument();
    expect(videoStore.find(UPLOADED_VIDEO_ID)?.sizeBytes).toBe(measuredBytes);
  });

  it('no crea metadata si streaming omite size_bytes', async () => {
    let metadataPosts = 0;
    server.use(
      http.post(UPLOAD_URL, () =>
        HttpResponse.json({ id: UPLOADED_VIDEO_ID, status: 'queued' }, {
          status: 202, headers: CORS_HEADERS,
        }),
      ),
      http.post(METADATA_URL, () => {
        metadataPosts += 1;
        return HttpResponse.json({ error: 'unexpected' }, { status: 500 });
      }),
    );
    await renderUploadPage();
    await chooseFile();
    await clickUpload();

    expect(await screen.findByRole('alert')).toHaveTextContent('size_bytes');
    expect(metadataPosts).toBe(0);
  });

  it('es privado si no se elige otra cosa', async () => {
    await renderUploadPage();
    await chooseFile();

    await clickUpload();

    await screen.findByRole('link', { name: 'Volver a los videos' });
    expect(videoStore.find(UPLOADED_VIDEO_ID)?.visibility).toBe('private');
  });

  it('respeta volver a privado después de elegir público', async () => {
    await renderUploadPage();
    await chooseFile();
    await userEvent.click(screen.getByRole('radio', { name: 'Todos' }));
    await userEvent.click(screen.getByRole('radio', { name: 'Solo yo' }));

    await clickUpload();

    await screen.findByRole('link', { name: 'Volver a los videos' });
    expect(videoStore.find(UPLOADED_VIDEO_ID)?.visibility).toBe('private');
  });

  it('propone el nombre del archivo como título sin pisar lo escrito', async () => {
    await renderUploadPage();
    await userEvent.type(screen.getByLabelText('Título'), 'Mi título');

    await chooseFile();

    expect(screen.getByLabelText('Título')).toHaveValue('Mi título');
  });

  it('exige elegir un archivo y un título', async () => {
    await renderUploadPage();

    await clickUpload();

    expect(screen.getByLabelText('Archivo de video')).toHaveAccessibleDescription(
      'Elige un video.',
    );
    expect(screen.getByLabelText('Título')).toHaveAccessibleDescription('Escribe un título.');
  });

  it('rechaza un archivo vacío', async () => {
    await renderUploadPage();
    await chooseFile(new File([], 'vacio.mp4', { type: 'video/mp4' }));

    await clickUpload();

    expect(screen.getByLabelText('Archivo de video')).toHaveAccessibleDescription(
      'Ese archivo está vacío.',
    );
  });

  it('bloquea un video que no cabe en la cuota, sin llamar a streaming', async () => {
    let uploads = 0;
    server.use(
      http.post(UPLOAD_URL, () => {
        uploads += 1;
        return HttpResponse.json({ id: UPLOADED_VIDEO_ID }, { status: 202 });
      }),
    );
    serveQuota(1000, 995);
    await renderUploadPage();
    await chooseFile();

    await clickUpload();

    expect(screen.getByLabelText('Archivo de video')).toHaveAccessibleDescription(
      'El video pesa 9 B y te quedan 5 B.',
    );
    expect(uploads).toBe(0);
  });

  it('impide subir al alcanzar la cuota', async () => {
    serveQuota(1000, 1000);
    await renderUploadPage();

    expect(await screen.findByRole('alert')).toHaveTextContent('Llegaste al límite');
    expect(screen.getByRole('button', { name: 'Subir video' })).toBeDisabled();
  });

  it('no permite subir si no se conoce la cuota', async () => {
    server.use(http.get(QUOTA_URL, () => HttpResponse.json({ error: 'x' }, { status: 500 })));
    renderApp('/upload', testSession);

    await screen.findByText('No se pudo consultar tu espacio.');

    expect(screen.getByRole('button', { name: 'Subir video' })).toBeDisabled();
  });

  it('muestra el avance, bloquea el formulario y permite cancelar', async () => {
    server.use(
      http.post(UPLOAD_URL, async () => {
        await delay('infinite');
        return HttpResponse.json({ id: UPLOADED_VIDEO_ID }, { status: 202 });
      }),
    );
    await renderUploadPage();
    await chooseFile();

    await clickUpload();

    expect(await screen.findByRole('progressbar')).toBeInTheDocument();
    expect(screen.getByLabelText('Título')).toBeDisabled();

    await userEvent.click(screen.getByRole('button', { name: 'Cancelar subida' }));

    expect(await screen.findByRole('button', { name: 'Subir video' })).toBeEnabled();
    expect(screen.queryByRole('alert')).not.toBeInTheDocument();
  });

  it('deshace la subida si metadata rechaza definitivamente la creación', async () => {
    let deletedPath = '';
    server.use(
      http.post(METADATA_URL, () =>
        HttpResponse.json({ error: 'bad_request', message: 'Datos inválidos' }, { status: 422 }),
      ),
      http.delete(`${UPLOAD_URL}/:videoId`, ({ request }) => {
        deletedPath = new URL(request.url).pathname;
        return new HttpResponse(null, { status: 204 });
      }),
    );
    await renderUploadPage();
    await chooseFile();

    await clickUpload();

    expect(await screen.findByRole('alert')).toHaveTextContent('Datos inválidos');
    expect(deletedPath).toBe(`/api/videos/${UPLOADED_VIDEO_ID}`);
  });

  it('recupera un alta aplicada cuando se pierde la primera respuesta de metadata', async () => {
    let posts = 0;
    let deletes = 0;
    server.use(
      http.post(METADATA_URL, async ({ request }) => {
        posts += 1;
        if (posts === 1) {
          const body = (await request.json()) as { id: string };
          expect(body.id).toBe(UPLOADED_VIDEO_ID);
          // Simula un commit en MongoDB seguido de una respuesta HTTP perdida.
          videoStore.add({
            ...testVideo,
            videoId: UPLOADED_VIDEO_ID,
            title: 'Clase de árboles',
            description: '',
            topic: '',
            tags: [],
            visibility: 'private',
            durationSeconds: DURATION_SECONDS,
            sizeBytes: video.size,
          });
          return HttpResponse.error();
        }
        // El segundo POST idempotente devuelve el recurso creado por el backend.
        const saved = videoStore.find(UPLOADED_VIDEO_ID);
        if (saved === undefined) {
          return HttpResponse.json({ error: 'temporary' }, { status: 503 });
        }
        return HttpResponse.json(toMetadataJson(saved), { status: 201 });
      }),
      http.delete(`${UPLOAD_URL}/:videoId`, () => {
        deletes += 1;
        return new HttpResponse(null, { status: 204 });
      }),
    );
    await renderUploadPage();
    await chooseFile();
    await clickUpload();
    expect(await screen.findByRole('link', { name: 'Volver a los videos' })).toBeInTheDocument();
    expect(posts).toBe(2);
    expect(deletes).toBe(0);
  });

  it('consulta metadata cuando el POST repetido tampoco devuelve resultado', async () => {
    let posts = 0;
    let deletes = 0;
    server.use(
      http.post(METADATA_URL, () => {
        posts += 1;
        if (posts === 1) {
          videoStore.add({
            ...testVideo,
            videoId: UPLOADED_VIDEO_ID,
            title: 'Clase de árboles',
            description: '',
            topic: '',
            tags: [],
            visibility: 'private',
            durationSeconds: DURATION_SECONDS,
            sizeBytes: video.size,
          });
          return HttpResponse.error();
        }
        return HttpResponse.json({ error: 'temporarily_unavailable' }, { status: 503 });
      }),
      http.delete(`${UPLOAD_URL}/:videoId`, () => {
        deletes += 1;
        return new HttpResponse(null, { status: 204 });
      }),
    );
    await renderUploadPage();
    await chooseFile();
    await clickUpload();
    expect(await screen.findByRole('link', { name: 'Volver a los videos' })).toBeInTheDocument();
    expect(posts).toBe(2);
    expect(deletes).toBe(0);
  });

  it('no borra streaming si ambos POST de metadata quedan inciertos', async () => {
    let posts = 0;
    let deletes = 0;
    server.use(
      http.post(METADATA_URL, () => {
        posts += 1;
        return HttpResponse.json({ error: 'unavailable' }, { status: 503 });
      }),
      http.get(`${METADATA_URL}/:videoId`, () => HttpResponse.error()),
      http.delete(`${UPLOAD_URL}/:videoId`, () => {
        deletes += 1;
        return new HttpResponse(null, { status: 204 });
      }),
    );
    await renderUploadPage();
    await chooseFile();
    await clickUpload();
    expect(await screen.findByRole('alert')).toHaveTextContent(UPLOADED_VIDEO_ID);
    expect(posts).toBe(2);
    expect(deletes).toBe(0);
  });

  it('guarda la operación incierta, la recupera después de recargar y no repite la subida', async () => {
    let metadataPosts = 0;
    let streamingPosts = 0;
    server.use(
      http.post(UPLOAD_URL, () => {
        streamingPosts += 1;
        return HttpResponse.json(
          { id: UPLOADED_VIDEO_ID, status: 'queued', size_bytes: video.size },
          { status: 202, headers: CORS_HEADERS },
        );
      }),
      http.post(METADATA_URL, () => {
        metadataPosts += 1;
        if (metadataPosts <= 2) {
          return HttpResponse.json({ error: 'unavailable' }, { status: 503 });
        }
        videoStore.add({
          ...testVideo,
          videoId: UPLOADED_VIDEO_ID,
          title: 'Clase de árboles',
          description: '',
          topic: '',
          tags: [],
          visibility: 'private',
          durationSeconds: DURATION_SECONDS,
          sizeBytes: video.size,
        });
        return HttpResponse.json(toMetadataJson(videoStore.find(UPLOADED_VIDEO_ID)!), { status: 201 });
      }),
      http.get(`${METADATA_URL}/:videoId`, () => HttpResponse.error()),
    );
    await renderUploadPage();
    await chooseFile();
    await clickUpload();
    expect(await screen.findByRole('alert')).toHaveTextContent(UPLOADED_VIDEO_ID);
    expect(screen.getByText(/Hay una subida pendiente/)).toBeInTheDocument();
    expect(streamingPosts).toBe(1);
    expect(window.localStorage.getItem(`tecflix.pending-metadata.v1.${String(testUser.id)}`))
      .toContain(UPLOADED_VIDEO_ID);

    // Simula una recarga: no volvemos a seleccionar ni enviar el archivo.
    cleanup();
    await renderUploadPage();
    expect(await screen.findByRole('link', { name: 'Volver a los videos' })).toBeInTheDocument();
    expect(streamingPosts).toBe(1);
    expect(metadataPosts).toBe(3);
    expect(window.localStorage.getItem(`tecflix.pending-metadata.v1.${String(testUser.id)}`))
      .toBeNull();
  });

  it('conserva una recuperación no resuelta y bloquea otra subida', async () => {
    server.use(
      http.post(METADATA_URL, () => HttpResponse.json({ error: 'unavailable' }, { status: 503 })),
      http.get(`${METADATA_URL}/:videoId`, () => HttpResponse.error()),
    );
    await renderUploadPage();
    await chooseFile();
    await clickUpload();
    expect(await screen.findByRole('alert')).toHaveTextContent(UPLOADED_VIDEO_ID);
    expect(screen.getByRole('button', { name: 'Subir video' })).toBeDisabled();
    cleanup();
    await renderUploadPage();
    expect(await screen.findByRole('button', { name: 'Reintentar registro pendiente' }))
      .toBeInTheDocument();
    expect(screen.getByRole('button', { name: 'Subir video' })).toBeDisabled();
  });

  it('muestra el error original aunque deshacer también falle', async () => {
    server.use(
      http.post(METADATA_URL, () =>
        HttpResponse.json({ error: 'bad_request', message: 'Falló Mongo' }, { status: 422 }),
      ),
      http.delete(`${UPLOAD_URL}/:videoId`, () => HttpResponse.error()),
    );
    await renderUploadPage();
    await chooseFile();

    await clickUpload();

    expect(await screen.findByRole('alert')).toHaveTextContent('Falló Mongo');
  });

  it.each([
    [413, 'quota_exceeded', 'no cabe en el espacio'],
    [413, 'upload_too_large', 'demasiado grande'],
    [503, 'transcoding_queue_full', 'ocupado'],
  ])('explica %d %s', async (status, error, text) => {
    server.use(
      http.post(UPLOAD_URL, () => HttpResponse.json({ error }, { status, headers: CORS_HEADERS })),
    );
    await renderUploadPage();
    await chooseFile();

    await clickUpload();

    expect(await screen.findByRole('alert')).toHaveTextContent(text);
  });

  it('actualiza la cuota tras subir', async () => {
    let quotaRequests = 0;
    server.use(
      http.get(QUOTA_URL, () => {
        quotaRequests += 1;
        return HttpResponse.json({ quotaBytes: 1000, usedBytes: 0 });
      }),
    );
    await renderUploadPage();
    await chooseFile();

    await clickUpload();

    await waitFor(() => {
      expect(quotaRequests).toBeGreaterThan(1);
    });
  });
});
