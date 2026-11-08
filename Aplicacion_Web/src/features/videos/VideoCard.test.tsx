import { render, screen } from '@testing-library/react';
import { MemoryRouter } from 'react-router';
import { describe, expect, it } from 'vitest';
import { testVideo } from '../../test/fixtures';
import type { Video } from '../../types/video';
import { VideoCard } from './VideoCard';

function renderCard(changes: Partial<Video> = {}, isOwn = false) {
  render(
    <MemoryRouter>
      <VideoCard video={{ ...testVideo, ...changes }} isOwn={isOwn} />
    </MemoryRouter>,
  );
}

describe('VideoCard', () => {
  it('muestra título, autor, tema, fecha, duración y acceso', () => {
    renderCard();

    expect(screen.getByRole('heading', { name: testVideo.title })).toBeInTheDocument();
    expect(screen.getByText(/Estructuras de datos, por ana, el .*2026/)).toBeInTheDocument();
    expect(screen.getByText('1:30')).toBeInTheDocument();
    expect(screen.getByText('Público')).toBeInTheDocument();
  });

  it('enlaza el título con la página del video', () => {
    renderCard();

    expect(screen.getByRole('link', { name: testVideo.title })).toHaveAttribute(
      'href',
      `/videos/${testVideo.videoId}`,
    );
  });

  it('indica que un video es privado', () => {
    renderCard({ visibility: 'private' });

    expect(screen.getByText('Privado')).toBeInTheDocument();
  });

  it('ofrece administrar solo los videos propios', () => {
    renderCard({}, true);

    expect(
      screen.getByRole('link', { name: `Administrar ${testVideo.title}` }),
    ).toBeInTheDocument();
  });

  it('no ofrece administrar videos ajenos', () => {
    renderCard({}, false);

    expect(screen.queryByText('Administrar')).not.toBeInTheDocument();
  });

  it('no muestra el estado de transcodificación en el catálogo', () => {
    renderCard();

    expect(screen.queryByText('Procesando')).not.toBeInTheDocument();
    expect(screen.queryByText('En cola')).not.toBeInTheDocument();
  });
});
