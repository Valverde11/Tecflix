import { Link } from 'react-router';
import type { VideoListQuery } from '../../types/video';
import { useAuth } from '../auth/useAuth';
import { Pagination } from './Pagination';
import { useVideoList } from './useVideoList';
import { VideoCard } from './VideoCard';
import styles from './VideoListBody.module.css';

interface VideoListBodyProps {
  readonly query: VideoListQuery;
  readonly searchForPage: (page: number) => string;
}

export function VideoListBody({ query, searchForPage }: VideoListBodyProps) {
  const { user } = useAuth();
  const { data, error, isFetching, isPlaceholderData, refetch } = useVideoList(query);

  if (data === undefined) {
    if (error === null) {
      return (
        <p role="status" className={styles.message}>
          Cargando videos…
        </p>
      );
    }
    return (
      <div role="alert" className={styles.message}>
        <p>No se pudieron cargar los videos. {error.message}</p>
        <button
          type="button"
          className={styles.retry}
          disabled={isFetching}
          onClick={() => {
            void refetch();
          }}
        >
          Intentar de nuevo
        </button>
      </div>
    );
  }

  if (data.total === 0) {
    return (
      <div className={styles.message}>
        <p>Todavía no hay videos.</p>
        <Link to="/upload">Subir el primero</Link>
      </div>
    );
  }

  // Pasa si se borraron videos o si se escribió la URL a mano: hay videos, pero no en esta página.
  if (data.items.length === 0) {
    return (
      <div className={styles.message}>
        <p>Esta página no tiene videos.</p>
        <Link to={{ search: searchForPage(1) }}>Ir a la primera página</Link>
      </div>
    );
  }

  return (
    <>
      <ul
        className={styles.list}
        aria-label="Lista de videos"
        aria-busy={isPlaceholderData}
        data-stale={isPlaceholderData}
      >
        {data.items.map((video) => (
          <li key={video.videoId}>
            <VideoCard video={video} isOwn={video.ownerId === user?.id} />
          </li>
        ))}
      </ul>
      <Pagination
        page={query.page}
        totalPages={Math.ceil(data.total / data.pageSize)}
        searchForPage={searchForPage}
      />
    </>
  );
}
