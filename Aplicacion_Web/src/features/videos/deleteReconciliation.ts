import { ApiError } from '../../api/ApiError';
import type { MetadataApi } from '../../api/MetadataApi';
import type { StreamingApi } from '../../api/StreamingApi';
import { finishPendingDeletion } from './pendingDeletion';

function ignoreNotFound(error: unknown): void {
  if (!(error instanceof ApiError && error.status === 404)) throw error;
}

/** Metadata primero, streaming después (contrato V1). DELETE 404 es éxito idempotente. */
export async function reconcileDeletion(
  ownerId: number,
  videoId: string,
  metadataApi: Pick<MetadataApi, 'deleteVideo'>,
  streamingApi: Pick<StreamingApi, 'deleteVideo'>,
): Promise<void> {
  await metadataApi.deleteVideo(videoId).catch(ignoreNotFound);
  await streamingApi.deleteVideo(videoId).catch(ignoreNotFound);
  finishPendingDeletion(ownerId, videoId);
}
