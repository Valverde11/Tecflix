import type { SyntheticEvent } from 'react';
import { useForm } from '../../hooks/useForm';
import type { Video } from '../../types/video';
import { useUpdateVideo } from './useUpdateVideo';
import { findVideoErrors, toFieldValues, toVideoDetails, type VideoField } from './videoFields';
import { VideoFieldsForm } from './VideoFieldsForm';
import styles from './VideoOwnerPanel.module.css';

interface VideoEditFormProps {
  readonly video: Video;
  readonly onClose: () => void;
}

export function VideoEditForm({ video, onClose }: VideoEditFormProps) {
  const form = useForm<VideoField>(toFieldValues(video), findVideoErrors);
  const update = useUpdateVideo(video.videoId);

  const submit = (event: SyntheticEvent) => {
    event.preventDefault();
    if (form.validate()) {
      update.mutate(toVideoDetails(form.values, video.visibility), { onSuccess: onClose });
    }
  };

  return (
    <form className={styles.form} onSubmit={submit} noValidate>
      <VideoFieldsForm form={form} />
      {update.error !== null && (
        <p role="alert" className={styles.error}>
          No se pudieron guardar los cambios. {update.error.message}
        </p>
      )}
      <div className={styles.actions}>
        <button type="submit" className={styles.primary} disabled={update.isPending}>
          Guardar cambios
        </button>
        <button type="button" className={styles.secondary} onClick={onClose}>
          Cancelar
        </button>
      </div>
    </form>
  );
}
