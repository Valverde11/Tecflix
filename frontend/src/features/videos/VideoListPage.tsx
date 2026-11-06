import type { VideoListQuery } from '../../types/video';
import { useVideoList } from './useVideoList';

const PAGE_SIZE = 10;

const QUERY: VideoListQuery = { page: 1, pageSize: PAGE_SIZE, sortBy: 'uploadedAt', order: 'desc' };

export function VideoListPage() {
  const { data } = useVideoList(QUERY);

  return (
    <>
      <h1>Videos</h1>
      <ul aria-label="Lista de videos">
        {data?.items.map((video) => (
          <li key={video.videoId}>{video.title}</li>
        ))}
      </ul>
    </>
  );
}
