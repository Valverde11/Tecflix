import { beforeEach, describe, expect, it, vi } from 'vitest';
import { describePlaybackError, PlaybackSession } from './PlaybackSession';

const MANIFEST_URL = 'http://streaming.test/api/videos/a/index.m3u8';

const shakaMock = vi.hoisted(() => {
  const state = {
    isSupported: true,
    loadError: null as Error | null,
    filters: [] as ((type: number, request: { headers: Record<string, string> }) => void)[],
    listeners: new Map<string, (event: unknown) => void>(),
  };
  const player = {
    attach: vi.fn(() => Promise.resolve()),
    load: vi.fn(() =>
      state.loadError === null ? Promise.resolve() : Promise.reject(state.loadError),
    ),
    destroy: vi.fn(() => Promise.resolve()),
    addEventListener: (type: string, listener: (event: unknown) => void) => {
      state.listeners.set(type, listener);
    },
    getNetworkingEngine: () => ({
      registerRequestFilter: (filter: (typeof state.filters)[number]) => {
        state.filters.push(filter);
      },
    }),
  };
  const Player = Object.assign(
    function Player() {
      return player;
    },
    { isBrowserSupported: () => state.isSupported },
  );
  return { state, player, Player };
});

vi.mock('shaka-player', () => ({
  default: {
    polyfill: { installAll: vi.fn() },
    Player: shakaMock.Player,
    util: { Error: { Code: { BAD_HTTP_STATUS: 1001, HTTP_ERROR: 1002, TIMEOUT: 1003 } } },
  },
}));

const onFailure = vi.fn<(message: string) => void>();

function createSession() {
  return new PlaybackSession(document.createElement('video'), onFailure);
}

function shakaError(code: number, data: readonly unknown[] = []) {
  return Object.assign(new Error('Fallo de Shaka'), { code, data });
}

beforeEach(() => {
  shakaMock.state.isSupported = true;
  shakaMock.state.loadError = null;
  shakaMock.state.filters.length = 0;
  shakaMock.state.listeners.clear();
  shakaMock.player.attach.mockClear();
  shakaMock.player.load.mockClear();
  shakaMock.player.destroy.mockClear();
  onFailure.mockReset();
});

describe('PlaybackSession', () => {
  it('conecta el reproductor al elemento de video y carga el manifiesto', async () => {
    await createSession().start(MANIFEST_URL, null);

    expect(shakaMock.player.attach).toHaveBeenCalledOnce();
    expect(shakaMock.player.load).toHaveBeenCalledWith(MANIFEST_URL);
    expect(onFailure).not.toHaveBeenCalled();
  });

  it('envía el token en las peticiones de manifiesto y segmentos', async () => {
    await createSession().start(MANIFEST_URL, 'jwt');
    const request = { headers: {} as Record<string, string> };

    shakaMock.state.filters[0]?.(0, request);

    expect(request.headers.Authorization).toBe('Bearer jwt');
  });

  it('no agrega el filtro si no hay token', async () => {
    await createSession().start(MANIFEST_URL, null);

    expect(shakaMock.state.filters).toHaveLength(0);
  });

  it('avisa si el navegador no es compatible y no crea el reproductor', async () => {
    shakaMock.state.isSupported = false;

    await createSession().start(MANIFEST_URL, null);

    expect(onFailure).toHaveBeenCalledWith(expect.stringContaining('navegador'));
    expect(shakaMock.player.attach).not.toHaveBeenCalled();
  });

  it('informa un fallo al cargar', async () => {
    shakaMock.state.loadError = shakaError(1002);

    await createSession().start(MANIFEST_URL, null);

    expect(onFailure).toHaveBeenCalledWith('No se pudo conectar con el servidor de video.');
  });

  it('informa un fallo durante la reproducción', async () => {
    await createSession().start(MANIFEST_URL, null);

    shakaMock.state.listeners.get('error')?.({ detail: shakaError(3016) });

    expect(onFailure).toHaveBeenCalledWith('No se pudo reproducir el video.');
  });

  it('libera el reproductor al detenerse', async () => {
    const session = createSession();
    await session.start(MANIFEST_URL, null);

    await session.stop();

    expect(shakaMock.player.destroy).toHaveBeenCalledOnce();
  });

  it('detener antes de empezar no falla', async () => {
    await expect(createSession().stop()).resolves.toBeUndefined();
  });

  it('no arranca si ya la detuvieron', async () => {
    const session = createSession();
    await session.stop();

    await session.start(MANIFEST_URL, null);

    expect(shakaMock.player.attach).not.toHaveBeenCalled();
  });

  it('no trata como error una carga interrumpida al salir de la página', async () => {
    const session = createSession();
    let rejectLoad: (reason: Error) => void = () => undefined;
    shakaMock.player.load.mockImplementationOnce(
      () =>
        new Promise((_resolve, reject) => {
          rejectLoad = reject;
        }),
    );
    const started = session.start(MANIFEST_URL, null);
    await vi.waitFor(() => {
      expect(shakaMock.player.load).toHaveBeenCalled();
    });

    await session.stop();
    rejectLoad(shakaError(7000));
    await started;

    expect(onFailure).not.toHaveBeenCalled();
  });
});

describe('describePlaybackError', () => {
  it.each([
    [shakaError(1001, ['url', 404]), 'ya no está disponible'],
    [shakaError(1001, ['url', 409]), 'se está procesando'],
    [shakaError(1001, ['url', 500]), 'No se pudo reproducir'],
    [shakaError(1002), 'No se pudo conectar'],
    [shakaError(1003), 'No se pudo conectar'],
    [shakaError(4000), 'No se pudo reproducir'],
    [new Error('otro'), 'No se pudo reproducir'],
    [null, 'No se pudo reproducir'],
  ])('traduce %j', (error, text) => {
    expect(describePlaybackError(error)).toContain(text);
  });
});
