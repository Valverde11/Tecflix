import { lazy, Suspense } from 'react';
import { useServices } from '../../hooks/useServices';
import type { Video } from '../../types/video';
import { useAuth } from '../auth/useAuth';
import { formatUploadDate } from '../videos/videoFormat';
import styles from './VideoDetails.module.css';

// Shaka pesa más que el resto de la web: solo se descarga al abrir un video.
const VideoPlayer = lazy(() =>
  import('./VideoPlayer').then(({ VideoPlayer: player }) => ({ default: player })),
);

const NOT_READY_MESSAGES = {
  queued: 'Este video está en cola para procesarse. Podrás verlo cuando termine.',
  processing: 'Este video se está procesando. Podrás verlo cuando termine.',
  failed: 'No se pudo procesar este video.',
};

function VideoScreen({ video }: { readonly video: Video }) {
  const { streamingApi } = useServices();
  const { token } = useAuth();

  if (video.status !== 'ready') {
    return <p className={styles.notReady}>{NOT_READY_MESSAGES[video.status]}</p>;
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
    </>
  );
}
