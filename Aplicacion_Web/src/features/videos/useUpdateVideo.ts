import { useMutation } from '@tanstack/react-query';
import { useServices } from '../../hooks/useServices';
import type { VideoDetails } from '../../types/video';

export function useUpdateVideo(videoId: string) {
  const { metadataApi, queryClient } = useServices();
  return useMutation({
    mutationFn: (details: VideoDetails) => metadataApi.updateVideo(videoId, details),
    onSuccess: async (video) => {
      // El detalle usa la respuesta directamente; la lista debe volver a pedirse a la API.
      queryClient.setQueryData(['video', videoId], video);
      await queryClient.invalidateQueries({ queryKey: ['videos'] });
    },
  });
}
