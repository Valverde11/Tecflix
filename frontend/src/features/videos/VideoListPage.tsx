import { useAuth } from '../auth/useAuth';
import { Pagination } from './Pagination';
import { useVideoList } from './useVideoList';
import { SORT_OPTIONS, useVideoListParams } from './useVideoListParams';
import { VideoCard } from './VideoCard';
import styles from './VideoListPage.module.css';

export function VideoListPage() {
  const { user } = useAuth();
  const { query, sortId, changeSort, searchForPage } = useVideoListParams();
  const { data } = useVideoList(query);

  return (
    <>
      <div className={styles.header}>
        <h1>Videos</h1>
        <label className={styles.sort}>
          Ordenar por
          <select
            value={sortId}
            onChange={(event) => {
              changeSort(event.target.value);
            }}
          >
            {SORT_OPTIONS.map(({ id, label }) => (
              <option key={id} value={id}>
                {label}
              </option>
            ))}
          </select>
        </label>
      </div>
      <ul className={styles.list} aria-label="Lista de videos">
        {data?.items.map((video) => (
          <li key={video.videoId}>
            <VideoCard video={video} isOwn={video.ownerId === user?.id} />
          </li>
        ))}
      </ul>
      {data !== undefined && (
        <Pagination
          page={query.page}
          totalPages={Math.ceil(data.total / data.pageSize)}
          searchForPage={searchForPage}
        />
      )}
    </>
  );
}
