import { Link } from 'react-router';
import styles from './Pagination.module.css';

interface PaginationProps {
  readonly page: number;
  readonly totalPages: number;
  readonly searchForPage: (page: number) => string;
}

export function Pagination({ page, totalPages, searchForPage }: PaginationProps) {
  if (totalPages <= 1) {
    return null;
  }

  return (
    <nav className={styles.pagination} aria-label="Páginas de videos">
      {page > 1 && <Link to={{ search: searchForPage(page - 1) }}>Anterior</Link>}
      <span className={styles.current}>
        Página {page} de {totalPages}
      </span>
      {page < totalPages && <Link to={{ search: searchForPage(page + 1) }}>Siguiente</Link>}
    </nav>
  );
}
