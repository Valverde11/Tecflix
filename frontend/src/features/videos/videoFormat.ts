const SECONDS_PER_MINUTE = 60;
const MINUTES_PER_HOUR = 60;
const SECONDS_PER_HOUR = SECONDS_PER_MINUTE * MINUTES_PER_HOUR;
const UNKNOWN_DURATION = '--:--';

const TOPIC_COLORS = ['yellow', 'cyan', 'green', 'magenta', 'red', 'blue'] as const;
const FNV_OFFSET_BASIS = 2_166_136_261;
const FNV_PRIME = 16_777_619;

const dateFormat = new Intl.DateTimeFormat('es-CR', { dateStyle: 'medium' });

function twoDigits(value: number): string {
  return String(value).padStart(2, '0');
}

/** Un video sin procesar aún no tiene duración conocida: la API manda 0. */
export function formatDuration(totalSeconds: number): string {
  if (totalSeconds <= 0) {
    return UNKNOWN_DURATION;
  }
  const hours = Math.floor(totalSeconds / SECONDS_PER_HOUR);
  const minutes = Math.floor(totalSeconds / SECONDS_PER_MINUTE) % MINUTES_PER_HOUR;
  const seconds = Math.floor(totalSeconds % SECONDS_PER_MINUTE);
  return hours > 0
    ? `${String(hours)}:${twoDigits(minutes)}:${twoDigits(seconds)}`
    : `${String(minutes)}:${twoDigits(seconds)}`;
}

export function formatUploadDate(isoDate: string): string {
  return dateFormat.format(new Date(isoDate));
}

/** Mismo tema, mismo color: el filo de cada fila permite reconocerlo de un vistazo. */
export function topicColor(topic: string): string {
  // FNV-1a: un hash polinómico con base 31 daría solo la suma de las letras al reducir a 6,
  // porque 31 mod 6 es 1. Math.imul multiplica en 32 bits, así que nunca desborda.
  let hash = FNV_OFFSET_BASIS;
  for (const character of topic) {
    hash = Math.imul(hash ^ character.charCodeAt(0), FNV_PRIME);
  }
  const unsignedHash = hash >>> 0;
  return `var(--bar-${TOPIC_COLORS[unsignedHash % TOPIC_COLORS.length]})`;
}
