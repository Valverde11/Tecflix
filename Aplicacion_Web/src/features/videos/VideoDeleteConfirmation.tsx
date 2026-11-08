import type { Video } from '../../types/video';
import { useDeleteVideo } from './useDeleteVideo';
import styles from './VideoOwnerPanel.module.css';

interface VideoDeleteConfirmationProps {
  readonly video: Video;
  readonly onCancel: () => void;
}

export function VideoDeleteConfirmation({ video, onCancel }: VideoDeleteConfirmationProps) {
  const remove = useDeleteVideo(video.videoId);

  return (
    <div className={styles.confirm}>
      <p>¿Eliminar «{video.title}»? No se puede deshacer y se liberará su espacio.</p>
      {remove.error !== null && (
        <p role="alert" className={styles.error}>
          No se pudo eliminar por completo. Intenta de nuevo. {remove.error.message}
        </p>
      )}
      <div className={styles.actions}>
        <button
          type="button"
          className={styles.danger}
          disabled={remove.isPending}
          onClick={() => {
            remove.mutate();
          }}
        >
          {remove.isPending ? 'Eliminando…' : 'Sí, eliminar'}
        </button>
        <button
          type="button"
          className={styles.secondary}
          disabled={remove.isPending}
          onClick={onCancel}
        >
          Cancelar
        </button>
      </div>
    </div>
  );
}
