import { Link, Outlet } from 'react-router';
import { useAuth } from '../features/auth/useAuth';
import { PendingDeletionRecovery } from '../features/videos/PendingDeletionRecovery';
import styles from './AppLayout.module.css';

export function AppLayout() {
  const { user, logout } = useAuth();

  return (
    <div className={styles.page}>
      <header className={styles.header}>
        <Link to="/videos" className={styles.brand}>
          Tecflix
        </Link>
        <Link to="/upload" className={styles.upload}>
          Subir video
        </Link>
        <span className={styles.username} title={user?.username}>
          {user?.username}
        </span>
        <button type="button" className={styles.logout} onClick={logout}>
          Cerrar sesión
        </button>
      </header>
      <main className={styles.content}>
        <PendingDeletionRecovery key={user?.id ?? 'anonymous'}/>
        <Outlet />
      </main>
    </div>
  );
}
