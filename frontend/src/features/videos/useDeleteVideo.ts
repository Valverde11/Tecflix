import { useMutation } from '@tanstack/react-query';
import { useNavigate } from 'react-router';
import { ApiError } from '../../api/ApiError';
import { useServices } from '../../hooks/useServices';
import { QUOTA_QUERY_KEY } from '../upload/useQuota';

const HTTP_NOT_FOUND = 404;

/** Lo que ya no existe cuenta como borrado: así un borrado a medias se puede repetir. */
function ignoreNotFound(error: unknown): void {
  if (!(error instanceof ApiError && error.status === HTTP_NOT_FOUND)) {
    throw error;
  }
}

export function useDeleteVideo(videoId: string) {
  const { metadataApi, streamingApi, queryClient } = useServices();
  const navigate = useNavigate();
  return useMutation({
    // Orden de SPEC 3.3. La cuota no se libera aquí: esa ruta exige la credencial de un
    // servicio, para que nadie pueda borrar su propio consumo. Lo hace streaming.
    mutationFn: async () => {
      await metadataApi.deleteVideo(videoId).catch(ignoreNotFound);
      await streamingApi.deleteVideo(videoId).catch(ignoreNotFound);
    },
    onSuccess: async () => {
      await Promise.all([
        queryClient.invalidateQueries({ queryKey: ['videos'] }),
        queryClient.invalidateQueries({ queryKey: [QUOTA_QUERY_KEY] }),
      ]);
      await navigate('/videos');
      queryClient.removeQueries({ queryKey: ['video', videoId] });
    },
  });
}
