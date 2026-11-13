import { useCallback, useEffect, useRef, useState } from 'react';
import { useMutation } from '@tanstack/react-query';
import { useAuth } from '../auth/useAuth';
import { useServices } from '../../hooks/useServices';
import { QUOTA_QUERY_KEY } from '../upload/useQuota';
import { reconcileDeletion } from './deleteReconciliation';
import { getPendingDeletions, PENDING_DELETION_CHANGED } from './pendingDeletion';

/** Recuperación automática al entrar en la aplicación, y reintento explícito si falla. */
export function PendingDeletionRecovery() {
  const { user } = useAuth();
  const ownerId = user?.id ?? null;
  const { metadataApi, streamingApi, queryClient } = useServices();
  const [ids, setIds] = useState<string[]>(() =>
    ownerId === null ? [] : getPendingDeletions(ownerId)
  );
  
  const [error, setError] = useState<string | null>(null);
  
  const recoveryStarted = useRef(false);

  const refresh = useCallback(() => {
    if (ownerId === null) {
      setIds([]);
      return;
    }
    try { setIds(getPendingDeletions(ownerId)); }
    catch (reason) { setIds([]); setError(reason instanceof Error ? reason.message : String(reason));
  }}, [ownerId]);

  const {
    mutate: recoverPending,
    isPending: recovering,
  } = useMutation({
    mutationFn: async () => {
      if (ownerId === null) {
        return;
      }
  
      const pending = getPendingDeletions(ownerId);
  
      for (const videoId of pending) {
        await reconcileDeletion(
          ownerId,
          videoId,
          metadataApi,
          streamingApi
        );
      }
    },
  
    onSuccess: async () => {
      setError(null);
  
      await Promise.all([
        queryClient.invalidateQueries({
          queryKey: ['videos'],
        }),
        queryClient.invalidateQueries({
          queryKey: [QUOTA_QUERY_KEY],
        }),
      ]);
  
      refresh();
    },
  
    onError: (reason: Error) => {
      setError(reason.message);
      refresh();
    },
  });

  useEffect(() => {
    window.addEventListener(PENDING_DELETION_CHANGED, refresh);

    return () => {
      window.removeEventListener(PENDING_DELETION_CHANGED, refresh);
    };
  }, [refresh]);

  useEffect(() => {
    if (ownerId === null || recoveryStarted.current) return;
    recoveryStarted.current = true;
    recoverPending();
  }, [ownerId, recoverPending]);

  if (ownerId === null || (ids.length === 0 && error === null)) return null;
  return (
    <section role="status" aria-label="Eliminaciones pendientes">
      {ids.length > 0 && (
        <p>Hay {ids.length} eliminación(es) pendiente(s). No vuelvas a eliminar estos videos manualmente.</p>
      )}
      {error !== null && <p role="alert">No se pudo terminar la eliminación: {error}</p>}
      {ids.length > 0 && (
        <button type="button" disabled={recovering} onClick={() => { recoverPending(); }}>
          {recovering ? 'Recuperando eliminaciones…' : 'Reintentar eliminaciones pendientes'}
        </button>
      )}
    </section>
  );
}
