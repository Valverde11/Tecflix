import { Navigate, Outlet, useLocation } from 'react-router';
import { useAuth } from './useAuth';

const DEFAULT_PATH = '/videos';

function returnPath(state: unknown): string {
  if (typeof state === 'object' && state !== null && 'from' in state) {
    return typeof state.from === 'string' ? state.from : DEFAULT_PATH;
  }
  return DEFAULT_PATH;
}

/**
 * Saca de login y registro a quien ya tiene sesión. Por eso esas pantallas no navegan al
 * terminar: basta con guardar la sesión y esta ruta hace la redirección.
 */
export function RedirectIfAuthenticated() {
  const { user } = useAuth();
  const location: { state: unknown } = useLocation();

  if (user !== null) {
    return <Navigate to={returnPath(location.state)} replace />;
  }
  return <Outlet />;
}
