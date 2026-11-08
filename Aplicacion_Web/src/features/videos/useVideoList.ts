import { keepPreviousData, useQuery } from '@tanstack/react-query';
import { useServices } from '../../hooks/useServices';
import type { VideoListQuery } from '../../types/video';

export function useVideoList(query: VideoListQuery) {
  const { metadataApi } = useServices();
  return useQuery({
    queryKey: ['videos', query],
    queryFn: () => metadataApi.listVideos(query),
    // Mantiene la página anterior a la vista mientras llega la nueva, sin parpadeo.
    placeholderData: keepPreviousData,
  });
}
