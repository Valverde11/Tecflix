import { useState } from 'react';
import type { Video, Visibility } from '../../types/video';
import { useUpdateVideo } from './useUpdateVideo';
import { VideoDeleteConfirmation } from './VideoDeleteConfirmation';
import { VideoEditForm } from './VideoEditForm';
import { toFieldValues, toVideoDetails } from './videoFields';
import styles from './VideoOwnerPanel.module.css';

const OPPOSITE_VISIBILITY: Readonly<Record<Visibility, Visibility>> = {
  public: 'private',
  private: 'public',
};

const VISIBILITY_ACTIONS: Readonly<Record<Visibility, string>> = {
  public: 'Hacer privado',
  private: 'Hacer público',
};

interface VideoOwnerPanelProps {
  readonly video: Video;
}

/** Acciones que solo ve el dueño. La API también lo comprueba: esto no es la protección. */
export function VideoOwnerPanel({ video }: VideoOwnerPanelProps) {
  const [isEditing, setIsEditing] = useState(false);
  const [isConfirmingDelete, setIsConfirmingDelete] = useState(false);
  const update = useUpdateVideo(video.videoId);

  if (isEditing) {
    return (
      <VideoEditForm
        video={video}
        onClose={() => {
          setIsEditing(false);
        }}
      />
    );
  }

  if (isConfirmingDelete) {
    return (
      <VideoDeleteConfirmation
        video={video}
        onCancel={() => {
          setIsConfirmingDelete(false);
        }}
      />
    );
  }

  return (
    <>
      <div className={styles.actions}>
        <button
          type="button"
          className={styles.secondary}
          onClick={() => {
            setIsEditing(true);
          }}
        >
          Editar datos
        </button>
        <button
          type="button"
          className={styles.secondary}
          disabled={update.isPending}
          onClick={() => {
            const visibility = OPPOSITE_VISIBILITY[video.visibility];
            update.mutate(toVideoDetails(toFieldValues(video), visibility));
          }}
        >
          {VISIBILITY_ACTIONS[video.visibility]}
        </button>
        <button
          type="button"
          className={`${styles.secondary} ${styles.dangerText}`}
          onClick={() => {
            setIsConfirmingDelete(true);
          }}
        >
          Eliminar video
        </button>
      </div>
      {update.error !== null && (
        <p role="alert" className={styles.error}>
          No se pudo cambiar el acceso. {update.error.message}
        </p>
      )}
    </>
  );
}
