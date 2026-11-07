import { useState } from 'react';
import type { Video } from '../../types/video';
import { VideoEditForm } from './VideoEditForm';
import styles from './VideoOwnerPanel.module.css';

interface VideoOwnerPanelProps {
  readonly video: Video;
}

/** Acciones que solo ve el dueño. La API también lo comprueba: esto no es la protección. */
export function VideoOwnerPanel({ video }: VideoOwnerPanelProps) {
  const [isEditing, setIsEditing] = useState(false);

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

  return (
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
    </div>
  );
}
