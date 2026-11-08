import { skipToken, useQuery } from '@tanstack/react-query';
import { useServices } from '../../hooks/useServices';
import { useAuth } from '../auth/useAuth';

export const QUOTA_QUERY_KEY = 'quota';

/** Consulta la API: el usuario guardado en la sesión queda desactualizado al subir o borrar. */
export function useQuota() {
  const { usersApi } = useServices();
  const { user } = useAuth();
  return useQuery({
    queryKey: [QUOTA_QUERY_KEY, user?.id],
    queryFn: user === null ? skipToken : () => usersApi.getQuota(user.id),
  });
}
