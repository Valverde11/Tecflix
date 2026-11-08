const BYTES_PER_UNIT = 1000;
const UNITS = ['B', 'KB', 'MB', 'GB', 'TB'];

const numberFormat = new Intl.NumberFormat('es-CR', { maximumFractionDigits: 1 });

/** Usa múltiplos de 1000 porque la cuota se define como 1 GB = 10⁹ bytes. */
export function formatBytes(bytes: number): string {
  let value = bytes;
  let unitIndex = 0;
  while (value >= BYTES_PER_UNIT && unitIndex < UNITS.length - 1) {
    value /= BYTES_PER_UNIT;
    unitIndex += 1;
  }
  return `${numberFormat.format(value)} ${UNITS[unitIndex]}`;
}
