import { beforeEach, describe, expect, it, vi } from 'vitest';
import { testSession } from '../test/fixtures';
import { SessionStore } from './SessionStore';

const STORAGE_KEY = 'tecflix.session';

beforeEach(() => {
  window.sessionStorage.clear();
});

describe('SessionStore', () => {
  it('empieza sin sesión', () => {
    expect(new SessionStore(window.sessionStorage).getSession()).toBeNull();
  });

  it('recupera la sesión al recargar la página', () => {
    new SessionStore(window.sessionStorage).setSession(testSession);

    expect(new SessionStore(window.sessionStorage).getSession()).toEqual(testSession);
  });

  it.each([['{no es json'], ['{"token":"x"}']])('descarta un dato inválido: %s', (saved) => {
    window.sessionStorage.setItem(STORAGE_KEY, saved);

    expect(new SessionStore(window.sessionStorage).getSession()).toBeNull();
    expect(window.sessionStorage.getItem(STORAGE_KEY)).toBeNull();
  });

  it('borra la sesión del almacenamiento al cerrarla', () => {
    const store = new SessionStore(window.sessionStorage);
    store.setSession(testSession);

    store.clear();

    expect(store.getSession()).toBeNull();
    expect(window.sessionStorage.getItem(STORAGE_KEY)).toBeNull();
  });

  it('devuelve el mismo objeto mientras no cambie, como exige React', () => {
    const store = new SessionStore(window.sessionStorage);
    store.setSession(testSession);

    expect(store.getSession()).toBe(store.getSession());
  });

  it('avisa los cambios hasta cancelar la suscripción', () => {
    const store = new SessionStore(window.sessionStorage);
    const listener = vi.fn<() => void>();
    const unsubscribe = store.subscribe(listener);

    store.setSession(testSession);
    unsubscribe();
    store.clear();

    expect(listener).toHaveBeenCalledOnce();
  });
});
