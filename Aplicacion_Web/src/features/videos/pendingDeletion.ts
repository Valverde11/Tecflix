/** Diario local para continuar DELETE tras una recarga. No contiene JWT ni datos personales. */
const PREFIX = 'tecflix.pending-deletions.v1.';
const VIDEO_ID = /^[a-f0-9]{32}$/;
export const PENDING_DELETION_CHANGED = 'tecflix:pending-deletion-changed';

function key(ownerId: number): string {
  if (!Number.isSafeInteger(ownerId) || ownerId < 1) {
    throw new Error('Identificador de propietario inválido');
  }
  return `${PREFIX}${String(ownerId)}`;
}

export function getPendingDeletions(ownerId: number): string[] {
  const raw = window.localStorage.getItem(key(ownerId));

  if (raw === null) {
    return [];
  }

  try {
    const parsed: unknown = JSON.parse(raw);

    if (!Array.isArray(parsed)) {
      throw new Error('Formato inválido');
    }

    const entries: unknown[] = parsed;

    const ids = entries.filter(
      (value): value is string =>
        typeof value === 'string' && VIDEO_ID.test(value)
    );

    const allValid = ids.length === entries.length;
    const noDuplicates = new Set(ids).size === ids.length;

    if (allValid && noDuplicates) {
      return ids;
    }
  } catch {
    // No ejecutar DELETE con datos locales corruptos.
  }

  throw new Error(
    'El registro de eliminaciones pendientes está dañado; revísalo antes de continuar'
  );
}

function persist(ownerId: number, ids: readonly string[]): void {
  if (ids.length === 0) {
    window.localStorage.removeItem(key(ownerId));
  } else {
    window.localStorage.setItem(key(ownerId), JSON.stringify(ids));
  }
  window.dispatchEvent(new Event(PENDING_DELETION_CHANGED));
}

/** Se escribe ANTES de llamar a metadata para soportar una interrupción en cualquier punto. */
export function addPendingDeletion(ownerId: number, videoId: string): void {
  if (!VIDEO_ID.test(videoId)) throw new Error('Identificador de video inválido');
  const ids = getPendingDeletions(ownerId);
  if (!ids.includes(videoId)) persist(ownerId, [...ids, videoId]);
}

/** Solo descartar el ID confirmado, sin afectar eliminaciones de otros videos. */
export function finishPendingDeletion(ownerId: number, videoId: string): void {
  const ids = getPendingDeletions(ownerId);
  if (ids.includes(videoId)) persist(ownerId, ids.filter((id) => id !== videoId));
}
