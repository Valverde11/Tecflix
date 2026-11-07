import { render, screen } from '@testing-library/react';
import userEvent from '@testing-library/user-event';
import { StrictMode } from 'react';
import { beforeEach, describe, expect, it, vi } from 'vitest';
import { VideoPlayer } from './VideoPlayer';

const MANIFEST_URL = 'http://streaming.test/api/videos/a/index.m3u8';

const session = vi.hoisted(() => ({
  instances: [] as { failWith: (message: string) => void }[],
  start: vi.fn<(url: string, token: string | null) => Promise<void>>(),
  stop: vi.fn<() => Promise<void>>(),
}));

vi.mock('./PlaybackSession', () => ({
  PlaybackSession: function PlaybackSession(
    this: object,
    _video: HTMLVideoElement,
    onFailure: (message: string) => void,
  ) {
    session.instances.push({ failWith: onFailure });
    return { start: session.start, stop: session.stop };
  },
}));

function renderPlayer(url = MANIFEST_URL) {
  return render(<VideoPlayer manifestUrl={url} token="jwt" title="Árboles B" />);
}

beforeEach(() => {
  session.instances.length = 0;
  session.start.mockReset().mockResolvedValue(undefined);
  session.stop.mockReset().mockResolvedValue(undefined);
});

describe('VideoPlayer', () => {
  it('inicia la reproducción con el manifiesto y el token', () => {
    renderPlayer();

    expect(session.start).toHaveBeenCalledWith(MANIFEST_URL, 'jwt');
    expect(screen.getByLabelText('Reproductor: Árboles B')).toHaveAttribute('controls');
  });

  it('destruye la sesión al salir de la página', () => {
    const { unmount } = renderPlayer();

    unmount();

    expect(session.stop).toHaveBeenCalledOnce();
  });

  it('cambia de sesión si cambia el video, sin dejar la anterior viva', () => {
    const { rerender } = renderPlayer();

    rerender(<VideoPlayer manifestUrl="http://otro" token="jwt" title="Otro" />);

    expect(session.stop).toHaveBeenCalledOnce();
    expect(session.start).toHaveBeenLastCalledWith('http://otro', 'jwt');
  });

  it('en StrictMode cada sesión iniciada también se detiene', () => {
    const { unmount } = render(
      <StrictMode>
        <VideoPlayer manifestUrl={MANIFEST_URL} token={null} title="Árboles B" />
      </StrictMode>,
    );

    unmount();

    expect(session.stop).toHaveBeenCalledTimes(session.start.mock.calls.length);
  });

  it('muestra el fallo y permite reintentar con una sesión nueva', async () => {
    renderPlayer();

    session.instances[0]?.failWith('No se pudo reproducir el video.');

    expect(await screen.findByRole('alert')).toHaveTextContent('No se pudo reproducir el video.');

    await userEvent.click(screen.getByRole('button', { name: 'Intentar de nuevo' }));

    expect(screen.queryByRole('alert')).not.toBeInTheDocument();
    expect(session.stop).toHaveBeenCalledOnce();
    expect(session.start).toHaveBeenCalledTimes(2);
  });
});
