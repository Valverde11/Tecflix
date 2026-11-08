import type { ReactNode } from 'react';
import styles from './AuthLayout.module.css';

interface AuthLayoutProps {
  readonly title: string;
  readonly children: ReactNode;
}

export function AuthLayout({ title, children }: AuthLayoutProps) {
  return (
    <div className={styles.page}>
      <div className={styles.brand}>
        <div className={styles.bars} aria-hidden="true" />
        <div className={styles.band}>
          <p className={styles.wordmark}>Tecflix</p>
          <p className={styles.tagline}>Sube tus videos y mira los que otros compartieron.</p>
        </div>
      </div>
      <main className={styles.panel}>
        <div className={styles.column}>
          <h1 className={styles.title}>{title}</h1>
          {children}
        </div>
      </main>
    </div>
  );
}
