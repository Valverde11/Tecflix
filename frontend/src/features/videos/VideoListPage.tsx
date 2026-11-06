import { SORT_OPTIONS, useVideoListParams } from './useVideoListParams';
import { VideoListBody } from './VideoListBody';
import styles from './VideoListPage.module.css';

export function VideoListPage() {
  const { query, sortId, changeSort, searchForPage } = useVideoListParams();

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
      <VideoListBody query={query} searchForPage={searchForPage} />
    </>
  );
}
