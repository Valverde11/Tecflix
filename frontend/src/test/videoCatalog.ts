import type { Video, VideoListQuery, VideoPage, VideoSortField } from '../types/video';
import { testUser, testVideo } from './fixtures';

const OTHER_OWNER_ID = 8;
const OTHER_AUTHOR = 'luis';
const SECONDS_PER_MINUTE = 60;

const LESSONS: readonly (readonly [title: string, topic: string, minutes: number])[] = [
  ['Listas enlazadas desde cero', 'Estructuras de datos', 18],
  ['Recorridos de árboles binarios', 'Estructuras de datos', 24],
  ['Tablas hash y colisiones', 'Estructuras de datos', 31],
  ['Punteros inteligentes en C++', 'Programación', 12],
  ['Qué es RAII y por qué importa', 'Programación', 9],
  ['Plantillas de C++ sin miedo', 'Programación', 27],
  ['Cómo funciona HLS', 'Video', 15],
  ['Transcodificar con FFmpeg', 'Video', 22],
  ['Codecs y contenedores', 'Video', 19],
  ['Introducción a REST', 'Redes', 14],
  ['Qué pasa al escribir una URL', 'Redes', 11],
  ['Autenticación con JWT', 'Redes', 26],
];

function dayOfOctober(index: number): string {
  const day = String(index + 1).padStart(2, '0');
  return `2026-10-${day}T15:00:00Z`;
}

function createLesson(index: number, part: number): Video {
  const [title, topic, minutes] = LESSONS[index % LESSONS.length] ?? ['', '', 0];
  const isOwn = index % 3 === 0;
  return {
    ...testVideo,
    videoId: String(part * LESSONS.length + index).padStart(32, '0'),
    ownerId: isOwn ? testUser.id : OTHER_OWNER_ID,
    author: isOwn ? testUser.username : OTHER_AUTHOR,
    title: part === 0 ? title : `${title} (parte ${String(part + 1)})`,
    topic,
    durationSeconds: minutes * SECONDS_PER_MINUTE,
    visibility: isOwn && index % 2 === 0 ? 'private' : 'public',
    uploadedAt: dayOfOctober(part * LESSONS.length + index),
  };
}

/** 25 videos: suficientes para varias páginas, con propios, ajenos, públicos y privados. */
export const videoCatalog: readonly Video[] = [
  testVideo,
  ...LESSONS.map((_lesson, index) => createLesson(index, 0)),
  ...LESSONS.map((_lesson, index) => createLesson(index, 1)),
];

function compareBy(field: VideoSortField) {
  return (a: Video, b: Video) => a[field].localeCompare(b[field], 'es');
}

/** Hace lo que le toca a la API de metadata; la web nunca ordena ni pagina. */
export function queryCatalog(videos: readonly Video[], query: VideoListQuery): VideoPage {
  const { page, pageSize, sortBy, order } = query;
  const sorted = videos.toSorted(compareBy(sortBy));
  const ordered = order === 'asc' ? sorted : sorted.toReversed();
  const start = (page - 1) * pageSize;
  return { items: ordered.slice(start, start + pageSize), page, pageSize, total: videos.length };
}
