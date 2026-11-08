import { useQuery } from '@tanstack/react-query';
import { useEffect } from 'react';
import { useServices } from '../../hooks/useServices';
import type { Video, VideoStatus } from '../../types/video';
import { QUOTA_QUERY_KEY } from '../upload/useQuota';

const POLL_INTERVAL_MS = 3000;

function isFinished(status: VideoStatus | undefined): boolean {
  return status === 'ready' || status === 'failed';
}

/**
 * Streaming es quien transcodifica, así que su estado manda sobre el de metadata. Solo se
 * consulta mientras metadata no diga `ready`, y deja de consultar al terminar.
 */
export function useTranscodingStatus(video: Video): VideoStatus {
  const { streamingApi, queryClient } = useServices();
  const { data: status } = useQuery({
    queryKey: ['transcoding', video.videoId],
    queryFn: () => streamingApi.getStatus(video.videoId),
    enabled: video.status !== 'ready',
    refetchInterval: (query) => (isFinished(query.state.data) ? false : POLL_INTERVAL_MS),
  });

  useEffect(() => {
    if (status === 'ready') {
      // Al terminar, streaming descuenta la cuota y la lista debe dejar de decir "Procesando".
      void queryClient.invalidateQueries({ queryKey: [QUOTA_QUERY_KEY] });
      void queryClient.invalidateQueries({ queryKey: ['videos'] });
    }
  }, [status, queryClient]);

  return status ?? video.status;
}
