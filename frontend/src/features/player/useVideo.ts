import { useQuery } from '@tanstack/react-query';
import { useServices } from '../../hooks/useServices';

export function useVideo(videoId: string) {
  const { metadataApi } = useServices();
  return useQuery({
    queryKey: ['video', videoId],
    queryFn: () => metadataApi.getVideo(videoId),
  });
}
