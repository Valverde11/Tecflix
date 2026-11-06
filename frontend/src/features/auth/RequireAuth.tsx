import { Navigate, Outlet, useLocation } from 'react-router';
import { useAuth } from './useAuth';

/** Protege las rutas hijas y recuerda a dónde iba la persona para volver tras iniciar sesión. */
export function RequireAuth() {
  const { user } = useAuth();
  const { pathname, search } = useLocation();

  if (user === null) {
    return <Navigate to="/login" replace state={{ from: `${pathname}${search}` }} />;
  }
  return <Outlet />;
}
