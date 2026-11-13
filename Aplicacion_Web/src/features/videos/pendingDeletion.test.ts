import { beforeEach, describe, expect, it } from 'vitest';
import { addPendingDeletion, finishPendingDeletion, getPendingDeletions } from './pendingDeletion';

const VIDEO_A = 'a'.repeat(32);
const VIDEO_B = 'b'.repeat(32);

beforeEach(() => {window.localStorage.clear()});

describe('pendingDeletion', () => {
  it('guarda varios IDs sin credenciales y no duplica un ID', () => {
    addPendingDeletion(5, VIDEO_A);
    addPendingDeletion(5, VIDEO_A);
    addPendingDeletion(5, VIDEO_B);
    expect(getPendingDeletions(5)).toEqual([VIDEO_A, VIDEO_B]);
    expect(window.localStorage.getItem('tecflix.pending-deletions.v1.5')).not.toContain('token');
  });

  it('solo elimina el ID completado y mantiene otros pendientes', () => {
    addPendingDeletion(5, VIDEO_A);
    addPendingDeletion(5, VIDEO_B);
    finishPendingDeletion(5, VIDEO_A);
    expect(getPendingDeletions(5)).toEqual([VIDEO_B]);
    finishPendingDeletion(5, VIDEO_B);
    expect(window.localStorage.getItem('tecflix.pending-deletions.v1.5')).toBeNull();
  });

  it('aísla propietarios diferentes', () => {
    addPendingDeletion(5, VIDEO_A);
    expect(getPendingDeletions(6)).toEqual([]);
  });

  it('no ejecuta borrados basados en IDs inválidos', () => {
    window.localStorage.setItem('tecflix.pending-deletions.v1.5', '["../secret"]');
    expect(() => getPendingDeletions(5)).toThrow('dañado');
    expect(() => {addPendingDeletion(5, 'malo')}).toThrow('inválido');
  });
});
