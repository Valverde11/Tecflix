import { Link } from 'react-router';
import type { Video, Visibility } from '../../types/video';
import styles from './VideoCard.module.css';
import { formatDuration, formatUploadDate, topicColor } from './videoFormat';

const VISIBILITY_LABELS: Readonly<Record<Visibility, string>> = {
  public: 'Público',
  private: 'Privado',
};

interface VideoCardProps {
  readonly video: Video;
  readonly isOwn: boolean;
}

export function VideoCard({ video, isOwn }: VideoCardProps) {
  const path = `/videos/${video.videoId}`;

  return (
    <article className={styles.card} style={{ borderLeftColor: topicColor(video.topic) }}>
      <p className={styles.duration}>{formatDuration(video.durationSeconds)}</p>
      <div className={styles.body}>
        <h2 className={styles.title}>
          <Link to={path}>{video.title}</Link>
        </h2>
        <p className={styles.details}>
          {video.topic}, por {video.author}, el {formatUploadDate(video.uploadedAt)}
        </p>
      </div>
      <div className={styles.aside}>
        <span>{VISIBILITY_LABELS[video.visibility]}</span>
        {isOwn && (
          <Link to={path} className={styles.manage} aria-label={`Administrar ${video.title}`}>
            Administrar
          </Link>
        )}
      </div>
    </article>
  );
}
