import type { FieldErrors } from '../../hooks/useForm';
import type { Video, VideoDetails } from '../../types/video';

// Límites de metadata (video_validation.cpp), que mide en bytes de UTF-8.
const MAX_TITLE_BYTES = 200;
const MAX_TOPIC_BYTES = 100;
const MAX_DESCRIPTION_BYTES = 5000;
const MAX_TAG_BYTES = 100;
const MAX_TAGS = 20;

export interface VideoFieldValues {
  readonly title: string;
  readonly topic: string;
  readonly description: string;
  readonly tags: string;
}

export type VideoField = keyof VideoFieldValues;

export const EMPTY_VIDEO_FIELDS: VideoFieldValues = {
  title: '',
  topic: '',
  description: '',
  tags: '',
};

function byteLength(text: string): number {
  return new TextEncoder().encode(text).length;
}

/** "a, b, a" → ["a", "b"]: el campo se escribe como texto y la API recibe una lista. */
export function parseTags(text: string): readonly string[] {
  const tags = text.split(',').map((tag) => tag.trim());
  return [...new Set(tags.filter((tag) => tag !== ''))];
}

function findTagsError(text: string): string | undefined {
  const tags = parseTags(text);
  if (tags.length > MAX_TAGS) {
    return `Usa como máximo ${String(MAX_TAGS)} etiquetas.`;
  }
  return tags.some((tag) => byteLength(tag) > MAX_TAG_BYTES)
    ? `Cada etiqueta puede tener hasta ${String(MAX_TAG_BYTES)} caracteres.`
    : undefined;
}

export function findVideoErrors(values: VideoFieldValues): FieldErrors<VideoField> {
  const { title, topic, description, tags } = values;
  const tagsError = findTagsError(tags);
  return {
    ...(title.trim() === '' && { title: 'Escribe un título.' }),
    ...(byteLength(title) > MAX_TITLE_BYTES && {
      title: `El título puede tener hasta ${String(MAX_TITLE_BYTES)} caracteres.`,
    }),
    ...(byteLength(topic) > MAX_TOPIC_BYTES && {
      topic: `El tema puede tener hasta ${String(MAX_TOPIC_BYTES)} caracteres.`,
    }),
    ...(byteLength(description) > MAX_DESCRIPTION_BYTES && {
      description: `La descripción puede tener hasta ${String(MAX_DESCRIPTION_BYTES)} caracteres.`,
    }),
    ...(tagsError !== undefined && { tags: tagsError }),
  };
}

export function toFieldValues(video: Video): VideoFieldValues {
  const { title, topic, description, tags } = video;
  return { title, topic, description, tags: tags.join(', ') };
}

export function toVideoDetails(
  values: VideoFieldValues,
  visibility: VideoDetails['visibility'],
): VideoDetails {
  return {
    title: values.title.trim(),
    topic: values.topic.trim(),
    description: values.description.trim(),
    tags: parseTags(values.tags),
    visibility,
  };
}
