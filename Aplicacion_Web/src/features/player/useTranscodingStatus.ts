import { useQuery } from '@tanstack/react-query';
import { useServices } from '../../hooks/useServices';
import type { VideoStatus } from '../../types/video';

const POLL_INTERVAL_MS = 3000;

function isFinished(status: VideoStatus | undefined): boolean {
  return status === 'ready' || status === 'failed';
}

/** Streaming es la única fuente de verdad del estado; se consulta en el detalle del video. */
export function useTranscodingStatus(videoId: string) {
  const { streamingApi } = useServices();
  return useQuery({
    queryKey: ['transcoding', videoId],
    queryFn: () => streamingApi.getStatus(videoId),
    refetchInterval: (query) =>
      isFinished(query.state.data) ? false : POLL_INTERVAL_MS,
  });
}
