import { Link, useParams } from 'react-router';
import { ApiError } from '../../api/ApiError';
import { useVideo } from './useVideo';
import styles from './VideoDetailPage.module.css';
import { VideoDetails } from './VideoDetails';

const HTTP_FORBIDDEN = 403;
const HTTP_NOT_FOUND = 404;

// Un video privado ajeno y uno inexistente se ven igual: no se revela cuál existe.
function isUnavailable(error: unknown): boolean {
  return (
    error instanceof ApiError &&
    (error.status === HTTP_NOT_FOUND || error.status === HTTP_FORBIDDEN)
  );
}

export function VideoDetailPage() {
  const { id = '' } = useParams();
  const { data: video, error, isFetching, refetch } = useVideo(id);

  return (
    <div className={styles.page}>
      <Link to="/videos" className={styles.back}>
        Volver a los videos
      </Link>
      {video !== undefined && <VideoDetails video={video} />}
      {video === undefined && error === null && <p role="status">Cargando video…</p>}
      {video === undefined && isUnavailable(error) && (
        <h1 className={styles.unavailable}>Este video no existe o no tienes acceso a él</h1>
      )}
      {video === undefined && error !== null && !isUnavailable(error) && (
        <div role="alert">
          <p>No se pudo cargar el video. {error.message}</p>
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
      )}
    </div>
  );
}
