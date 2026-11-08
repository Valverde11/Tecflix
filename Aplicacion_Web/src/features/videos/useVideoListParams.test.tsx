import { act, renderHook } from '@testing-library/react';
import type { ReactNode } from 'react';
import { MemoryRouter, useLocation } from 'react-router';
import { describe, expect, it } from 'vitest';
import { useVideoListParams } from './useVideoListParams';

/** Devuelve el hook junto con la URL actual, para comprobar qué escribe en ella. */
function renderParams(search: string) {
  function wrapper({ children }: { readonly children: ReactNode }) {
    return <MemoryRouter initialEntries={[`/videos${search}`]}>{children}</MemoryRouter>;
  }
  return renderHook(() => ({ params: useVideoListParams(), location: useLocation() }), { wrapper });
}

describe('useVideoListParams', () => {
  it('sin parámetros pide la primera página de los más recientes', () => {
    const { result } = renderParams('');

    expect(result.current.params.query).toEqual({
      page: 1,
      pageSize: 10,
      sortBy: 'uploadedAt',
      order: 'desc',
    });
    expect(result.current.params.sortId).toBe('uploadedAt-desc');
  });

  it('lee la página y el orden de la URL', () => {
    const { result } = renderParams('?page=3&sort=title-asc');

    expect(result.current.params.query).toMatchObject({ page: 3, sortBy: 'title', order: 'asc' });
  });

  it.each(['0', '-2', '1.5', 'abc', '1e400'])('ignora la página inválida %s', (page) => {
    const { result } = renderParams(`?page=${page}`);

    expect(result.current.params.query.page).toBe(1);
  });

  it('ignora un orden desconocido', () => {
    const { result } = renderParams('?sort=size-desc');

    expect(result.current.params.sortId).toBe('uploadedAt-desc');
  });

  it('al cambiar el orden vuelve a la primera página', () => {
    const { result } = renderParams('?page=4');

    act(() => {
      result.current.params.changeSort('author-asc');
    });

    expect(result.current.location.search).toBe('?sort=author-asc');
    expect(result.current.params.query).toMatchObject({ page: 1, sortBy: 'author' });
  });

  it('deja la URL limpia al volver a los valores por defecto', () => {
    const { result } = renderParams('?sort=title-desc');

    act(() => {
      result.current.params.changeSort('uploadedAt-desc');
    });

    expect(result.current.location.search).toBe('');
  });

  it('arma los enlaces de página conservando el orden', () => {
    const { result } = renderParams('?sort=title-desc');

    expect(result.current.params.searchForPage(2)).toBe('?page=2&sort=title-desc');
    expect(result.current.params.searchForPage(1)).toBe('?sort=title-desc');
  });
});
