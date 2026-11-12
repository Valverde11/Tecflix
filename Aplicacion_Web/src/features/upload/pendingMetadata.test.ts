import { beforeEach, describe, expect, it } from 'vitest';
import { clearPendingMetadata, loadPendingMetadata, savePendingMetadata } from './pendingMetadata';

const input = {
  videoId: 'a'.repeat(32),
  title: 'Video de prueba',
  description: '',
  topic: '',
  tags: ['tecflix'],
  visibility: 'private' as const,
  durationSeconds: 15,
  sizeBytes: 1024,
};

beforeEach(() => window.localStorage.clear());

describe('pendingMetadata', () => {
  it('sobrevive a una nueva lectura y no contiene credenciales ni archivo', () => {
    savePendingMetadata(7, input);
    expect(loadPendingMetadata(7)?.input).toEqual(input);
    const raw = window.localStorage.getItem('tecflix.pending-metadata.v1.7');
    expect(raw).not.toContain('Bearer');
    expect(raw).not.toContain('password');
  });

  it('aísla cada cuenta y rechaza un segundo registro de la misma cuenta', () => {
    savePendingMetadata(7, input);
    expect(loadPendingMetadata(8)).toBeNull();
    expect(() => savePendingMetadata(7, { ...input, videoId: 'b'.repeat(32) }))
      .toThrow('Ya existe una subida pendiente');
  });

  it('solo limpia el identificador esperado', () => {
    savePendingMetadata(7, input);
    clearPendingMetadata(7, 'b'.repeat(32));
    expect(loadPendingMetadata(7)).not.toBeNull();
    clearPendingMetadata(7, input.videoId);
    expect(loadPendingMetadata(7)).toBeNull();
  });

  it('descarta registros alterados y no los utiliza para reintentar', () => {
    window.localStorage.setItem('tecflix.pending-metadata.v1.7', JSON.stringify({
      version: 1, ownerId: 8, input,
    }));
    expect(loadPendingMetadata(7)).toBeNull();
    expect(window.localStorage.getItem('tecflix.pending-metadata.v1.7')).toBeNull();
  });
});
