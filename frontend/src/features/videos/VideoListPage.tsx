import type { VideoListQuery } from '../../types/video';
import { useAuth } from '../auth/useAuth';
import { useVideoList } from './useVideoList';
import { VideoCard } from './VideoCard';
import styles from './VideoListPage.module.css';

const PAGE_SIZE = 10;

const QUERY: VideoListQuery = { page: 1, pageSize: PAGE_SIZE, sortBy: 'uploadedAt', order: 'desc' };

export function VideoListPage() {
  const { user } = useAuth();
  const { data } = useVideoList(QUERY);

  return (
    <>
      <h1>Videos</h1>
      <ul className={styles.list} aria-label="Lista de videos">
        {data?.items.map((video) => (
          <li key={video.videoId}>
            <VideoCard video={video} isOwn={video.ownerId === user?.id} />
          </li>
        ))}
      </ul>
    </>
  );
}
