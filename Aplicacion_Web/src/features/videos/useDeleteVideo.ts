import { useMutation } from '@tanstack/react-query';
import { useNavigate } from 'react-router';
import { useAuth } from '../auth/useAuth';
import { addPendingDeletion } from './pendingDeletion';
import { reconcileDeletion } from './deleteReconciliation';
import { useServices } from '../../hooks/useServices';
import { QUOTA_QUERY_KEY } from '../upload/useQuota';

export function useDeleteVideo(videoId: string) {
  const { metadataApi, streamingApi, queryClient } = useServices();
  const navigate = useNavigate();
  const { user } = useAuth();
  return useMutation({
    // Orden de SPEC 3.3. La cuota no se libera aquí: esa ruta exige la credencial de un
    // servicio, para que nadie pueda borrar su propio consumo. Lo hace streaming.
    mutationFn: async () => {
      if (user === null) throw new Error('Debes iniciar sesión para eliminar videos');
      addPendingDeletion(user.id, videoId);
      await reconcileDeletion(user.id, videoId, metadataApi, streamingApi);
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
