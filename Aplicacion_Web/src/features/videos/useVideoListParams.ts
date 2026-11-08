import { useSearchParams } from 'react-router';
import type { SortOrder, VideoListQuery, VideoSortField } from '../../types/video';

const PAGE_SIZE = 10;
const FIRST_PAGE = 1;

interface SortOption {
  readonly id: string;
  readonly label: string;
  readonly sortBy: VideoSortField;
  readonly order: SortOrder;
}

const DEFAULT_SORT: SortOption = {
  id: 'uploadedAt-desc',
  label: 'Más recientes',
  sortBy: 'uploadedAt',
  order: 'desc',
};

export const SORT_OPTIONS: readonly SortOption[] = [
  DEFAULT_SORT,
  { id: 'uploadedAt-asc', label: 'Más antiguos', sortBy: 'uploadedAt', order: 'asc' },
  { id: 'title-asc', label: 'Título de la A a la Z', sortBy: 'title', order: 'asc' },
  { id: 'title-desc', label: 'Título de la Z a la A', sortBy: 'title', order: 'desc' },
  { id: 'author-asc', label: 'Autor de la A a la Z', sortBy: 'author', order: 'asc' },
  { id: 'author-desc', label: 'Autor de la Z a la A', sortBy: 'author', order: 'desc' },
];

// La URL la puede editar cualquiera: lo que no sea válido vuelve al valor por defecto.
function parsePage(text: string | null): number {
  const page = Number(text);
  return Number.isSafeInteger(page) && page >= FIRST_PAGE ? page : FIRST_PAGE;
}

function parseSort(id: string | null): SortOption {
  return SORT_OPTIONS.find((option) => option.id === id) ?? DEFAULT_SORT;
}

/** Omite los valores por defecto para que la URL de la vista inicial quede limpia. */
function buildParams(page: number, sortId: string): URLSearchParams {
  const params = new URLSearchParams();
  if (page !== FIRST_PAGE) {
    params.set('page', String(page));
  }
  if (sortId !== DEFAULT_SORT.id) {
    params.set('sort', sortId);
  }
  return params;
}

/** La URL es la fuente de verdad: recargar, compartir el enlace y el botón Atrás funcionan. */
export function useVideoListParams() {
  const [searchParams, setSearchParams] = useSearchParams();
  const sort = parseSort(searchParams.get('sort'));
  const page = parsePage(searchParams.get('page'));
  const query: VideoListQuery = {
    page,
    pageSize: PAGE_SIZE,
    sortBy: sort.sortBy,
    order: sort.order,
  };

  return {
    query,
    sortId: sort.id,
    changeSort: (sortId: string) => {
      // El orden nuevo reparte los videos distinto: seguir en la página 5 no tendría sentido.
      setSearchParams(buildParams(FIRST_PAGE, sortId));
    },
    searchForPage: (targetPage: number) => `?${buildParams(targetPage, sort.id).toString()}`,
  };
}
