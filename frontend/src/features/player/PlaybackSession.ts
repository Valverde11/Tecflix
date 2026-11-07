import shaka from 'shaka-player';

const HTTP_NOT_FOUND = 404;
const HTTP_CONFLICT = 409;
const HTTP_STATUS_INDEX = 1;

const UNSUPPORTED_BROWSER = 'Este navegador no puede reproducir el video. Prueba con otro.';
const VIDEO_NOT_FOUND = 'El archivo de este video ya no está disponible.';
const VIDEO_NOT_READY = 'Este video todavía se está procesando. Intenta en un momento.';
const CONNECTION_FAILED = 'No se pudo conectar con el servidor de video.';
const PLAYBACK_FAILED = 'No se pudo reproducir el video.';

interface ShakaFailure {
  readonly code: shaka.util.Error.Code;
  readonly data: readonly unknown[];
}

function isShakaFailure(error: unknown): error is ShakaFailure {
  return typeof error === 'object' && error !== null && 'code' in error && 'data' in error;
}

export function describePlaybackError(error: unknown): string {
  if (!isShakaFailure(error)) {
    return PLAYBACK_FAILED;
  }
  const { BAD_HTTP_STATUS, HTTP_ERROR, TIMEOUT } = shaka.util.Error.Code;
  if (error.code === HTTP_ERROR || error.code === TIMEOUT) {
    return CONNECTION_FAILED;
  }
  if (error.code === BAD_HTTP_STATUS) {
    const status = error.data[HTTP_STATUS_INDEX];
    if (status === HTTP_NOT_FOUND) {
      return VIDEO_NOT_FOUND;
    }
    if (status === HTTP_CONFLICT) {
      return VIDEO_NOT_READY;
    }
  }
  return PLAYBACK_FAILED;
}

/** Una reproducción: aísla a Shaka del resto de la web y garantiza que se libere. */
export class PlaybackSession {
  private readonly video: HTMLVideoElement;
  private readonly onFailure: (message: string) => void;
  private player: shaka.Player | null = null;
  private stopped = false;

  constructor(video: HTMLVideoElement, onFailure: (message: string) => void) {
    this.video = video;
    this.onFailure = onFailure;
  }

  async start(manifestUrl: string, token: string | null): Promise<void> {
    shaka.polyfill.installAll();
    if (!shaka.Player.isBrowserSupported()) {
      this.onFailure(UNSUPPORTED_BROWSER);
      return;
    }

    const player = new shaka.Player();
    this.player = player;
    player.addEventListener('error', (event) => {
      this.report((event as Event & { detail?: unknown }).detail);
    });
    try {
      await player.attach(this.video);
      if (token !== null) {
        // Shaka hace sus propias peticiones: HttpClient no pasa por aquí.
        player.getNetworkingEngine()?.registerRequestFilter((_type, request) => {
          request.headers.Authorization = `Bearer ${token}`;
        });
      }
      await player.load(manifestUrl);
    } catch (error) {
      this.report(error);
    }
  }

  async stop(): Promise<void> {
    this.stopped = true;
    await this.player?.destroy();
  }

  private report(error: unknown): void {
    // Detener a mitad de la carga la interrumpe y Shaka lo informa como error; no lo es.
    if (!this.stopped) {
      this.onFailure(describePlaybackError(error));
    }
  }
}
