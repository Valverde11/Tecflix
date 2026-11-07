import { formatBytes } from './formatBytes';
import styles from './QuotaMeter.module.css';
import { useQuota } from './useQuota';

const FULL_PERCENT = 100;
const NEARLY_FULL_PERCENT = 90;

export function QuotaMeter() {
  const { data: quota, isError } = useQuota();

  if (quota === undefined) {
    return <p className={styles.note}>{isError ? 'No se pudo consultar tu espacio.' : ' '}</p>;
  }

  const { quotaBytes, usedBytes } = quota;
  const percent = Math.min(FULL_PERCENT, (usedBytes / quotaBytes) * FULL_PERCENT);
  const summary = `${formatBytes(usedBytes)} de ${formatBytes(quotaBytes)} usados`;

  return (
    <div className={styles.quota}>
      <p className={styles.note}>{summary}</p>
      <div
        role="meter"
        aria-label="Espacio usado"
        aria-valuemin={0}
        aria-valuemax={FULL_PERCENT}
        aria-valuenow={Math.round(percent)}
        aria-valuetext={summary}
        className={styles.track}
      >
        <div
          className={styles.fill}
          data-nearly-full={percent >= NEARLY_FULL_PERCENT}
          style={{ width: `${String(percent)}%` }}
        />
      </div>
    </div>
  );
}
