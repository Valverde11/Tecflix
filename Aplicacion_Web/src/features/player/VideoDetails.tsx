import { lazy, Suspense } from 'react';
import { useServices } from '../../hooks/useServices';
import type { Video, VideoStatus } from '../../types/video';
import { useAuth } from '../auth/useAuth';
import { formatUploadDate } from '../videos/videoFormat';
import { VideoOwnerPanel } from '../videos/VideoOwnerPanel';
import { useTranscodingStatus } from './useTranscodingStatus';
import styles from './VideoDetails.module.css';

// Shaka pesa más que el resto de la web: solo se descarga al abrir un video.
const VideoPlayer = lazy(() =>
  import('./VideoPlayer').then(({ VideoPlayer: player }) => ({ default: player })),
);

const NOT_READY_MESSAGES: Readonly<Record<Exclude<VideoStatus, 'ready'>, string>> = {
  queued: 'Este video está en cola para procesarse. Empezará solo cuando termine.',
  processing: 'Este video se está procesando. Empezará solo cuando termine.',
  failed: 'No se pudo procesar este video.',
};

function VideoScreen({ video }: { readonly video: Video }) {
  const { streamingApi } = useServices();
  const { token } = useAuth();
  const { data: status, isLoading, isError, refetch } = useTranscodingStatus(video.videoId);

  if (isLoading) {
    return (
      <p role="status" className={styles.notReady}>
        Consultando el estado del video…
      </p>
    );
  }

  if (isError && status === undefined) {
    return (
      <div role="alert" className={styles.notReady}>
        <p>No se pudo consultar el estado del video.</p>
        <button type="button" onClick={() => void refetch()}>
          Reintentar
        </button>
      </div>
    );
  }

  if (status !== 'ready') {
    return (
      <p role="status" className={styles.notReady}>
        {status === undefined ? 'Estado del video no disponible.' : NOT_READY_MESSAGES[status]}
      </p>
    );
  }

  return (
    <Suspense fallback={<p className={styles.notReady}>Cargando reproductor…</p>}>
      <VideoPlayer
        manifestUrl={streamingApi.getManifestUrl(video.videoId)}
        token={token}
        title={video.title}
      />
    </Suspense>
  );
}

export function VideoDetails({ video }: { readonly video: Video }) {
  const { user } = useAuth();

  return (
    <>
      <VideoScreen video={video} />
      <h1 className={styles.title}>{video.title}</h1>
      <p className={styles.byline}>
        {video.topic}, por {video.author}, el {formatUploadDate(video.uploadedAt)}.{' '}
        {video.visibility === 'public' ? 'Público' : 'Privado'}
      </p>
      {video.description !== '' && <p className={styles.description}>{video.description}</p>}
      {video.tags.length > 0 && (
        <ul className={styles.tags} aria-label="Etiquetas">
          {video.tags.map((tag) => (
            <li key={tag}>{tag}</li>
          ))}
        </ul>
      )}
      {video.ownerId === user?.id && <VideoOwnerPanel video={video} />}
    </>
  );
}
