const SECONDS_PER_MINUTE = 60;
const MINUTES_PER_HOUR = 60;
const SECONDS_PER_HOUR = SECONDS_PER_MINUTE * MINUTES_PER_HOUR;
const UNKNOWN_DURATION = '--:--';

const TOPIC_COLORS = ['yellow', 'cyan', 'green', 'magenta', 'red', 'blue'] as const;
const HASH_MULTIPLIER = 31;

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
  const hash = [...topic].reduce(
    (total, character) => total * HASH_MULTIPLIER + (character.codePointAt(0) ?? 0),
    0,
  );
  return `var(--bar-${TOPIC_COLORS[hash % TOPIC_COLORS.length] ?? 'blue'})`;
}
