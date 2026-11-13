import type { NewVideo } from '../../types/video';

const STORAGE_PREFIX = 'tecflix.pending-metadata.v1.';
const VIDEO_ID = /^[a-f0-9]{32}$/;

export interface PendingMetadata {
  readonly version: 1;
  readonly ownerId: number;
  readonly input: NewVideo;
}

function key(ownerId: number): string {
  return `${STORAGE_PREFIX}${String(ownerId)}`;
}

function isValid(value: unknown, ownerId: number): value is PendingMetadata {
  if (value === null || typeof value !== 'object' || Array.isArray(value)) return false;
  const record = value as Record<string, unknown>;
  if (record.version !== 1 || !Number.isSafeInteger(ownerId) || ownerId <= 0 ||
      record.ownerId !== ownerId) return false;
  const candidate = record.input;
  if (candidate == null || typeof candidate !== 'object' || Array.isArray(candidate)) return false;
  const input = candidate as Record<string, unknown>;
  return (
    typeof input.videoId === 'string' &&
    VIDEO_ID.test(input.videoId) &&
    typeof input.title === 'string' &&
    typeof input.description === 'string' &&
    typeof input.topic === 'string' &&
    (input.visibility === 'public' || input.visibility === 'private') &&
    Array.isArray(input.tags) &&
    input.tags.every((tag: unknown) => typeof tag === 'string') &&
    typeof input.sizeBytes === 'number' &&
    Number.isSafeInteger(input.sizeBytes) &&
    input.sizeBytes > 0 &&
    typeof input.durationSeconds === 'number' &&
    Number.isFinite(input.durationSeconds) &&
    input.durationSeconds >= 0
  );
}

/** Un registro por cuenta. No se guarda el archivo, la contraseña ni el JWT. */
export function loadPendingMetadata(ownerId: number): PendingMetadata | null {
  const stored = window.localStorage.getItem(key(ownerId));
  if (stored === null) return null;
  try {
    const value: unknown = JSON.parse(stored);
    if (isValid(value, ownerId)) return value;
  } catch {
    // Un registro inválido no puede autorizar ni reintentar operaciones.
  }
  window.localStorage.removeItem(key(ownerId));
  return null;
}

export function savePendingMetadata(ownerId: number, input: NewVideo): void {
  const record: PendingMetadata = { version: 1, ownerId, input };
  if (!isValid(record, ownerId)) throw new Error('Registro de metadata pendiente inválido');
  if (loadPendingMetadata(ownerId) !== null) {
    throw new Error('Ya existe una subida pendiente de recuperar para esta cuenta');
  }
  window.localStorage.setItem(key(ownerId), JSON.stringify(record));
}

/** Solo descartar el registro que corresponde al mismo video; no afectar uno posterior. */
export function clearPendingMetadata(ownerId: number, videoId: string): void {
  if (loadPendingMetadata(ownerId)?.input.videoId === videoId) {
    window.localStorage.removeItem(key(ownerId));
  }
}
