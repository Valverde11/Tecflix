import { useMutation } from '@tanstack/react-query';
import { useEffect, useRef, useState, type SyntheticEvent } from 'react';
import { useNavigate } from 'react-router';
import { ApiError, NETWORK_ERROR_STATUS } from '../../api/ApiError';
import type { MetadataApi } from '../../api/MetadataApi';
import { UPLOAD_CANCELLED } from '../../api/HttpClient';
import { useForm } from '../../hooks/useForm';
import { useServices } from '../../hooks/useServices';
import type { NewVideo, Video, VideoDetails, Visibility } from '../../types/video';
import {
  EMPTY_VIDEO_FIELDS,
  findVideoErrors,
  toVideoDetails,
  type VideoField,
} from '../videos/videoFields';
import { formatBytes } from './formatBytes';
import { readVideoDuration } from './readVideoDuration';
import { QUOTA_QUERY_KEY, useQuota } from './useQuota';

const MESSAGES_BY_CODE = new Map([
  ['quota_exceeded', 'El video no cabe en el espacio que te queda.'],
  ['upload_too_large', 'El video es demasiado grande.'],
  ['transcoding_queue_full', 'El servidor está ocupado. Intenta en unos minutos.'],
  ['network_error', 'No se pudo conectar con el servidor. Intenta de nuevo.'],
]);

function describeUploadError(error: Error): string {
  const message = error instanceof ApiError ? MESSAGES_BY_CODE.get(error.code) : undefined;
  return message ?? `No se pudo subir el video. ${error.message}`;
}

function isCancelled(error: unknown): boolean {
  return error instanceof ApiError && error.code === UPLOAD_CANCELLED;
}

function findFileError(file: File | null, availableBytes: number): string | null {
  if (file === null) {
    return 'Elige un video.';
  }
  if (file.size === 0) {
    return 'Ese archivo está vacío.';
  }
  return file.size > availableBytes
    ? `El video pesa ${formatBytes(file.size)} y te quedan ${formatBytes(availableBytes)}.`
    : null;
}

/** Un timeout, un 5xx o una respuesta ilegible no demuestra que MongoDB rechazó el alta. */
function isAmbiguousCreateFailure(error: unknown): boolean {
  return !(error instanceof ApiError) ||
    error.status === NETWORK_ERROR_STATUS || error.status === 409 || error.status >= 500 ||
    error.code === 'invalid_response';
}

function matchesCreateRequest(saved: Video, expected: NewVideo): boolean {
  return saved.videoId === expected.videoId &&
    saved.title === expected.title &&
    saved.description === expected.description &&
    saved.topic === expected.topic &&
    saved.visibility === expected.visibility &&
    saved.durationSeconds === expected.durationSeconds &&
    saved.sizeBytes === expected.sizeBytes &&
    saved.tags.length === expected.tags.length &&
    saved.tags.every((tag, index) => tag === expected.tags[index]);
}

/** Solo repetir el POST de metadata: streaming no acepta un segundo POST idempotente de archivo. */
async function createMetadataWithReconciliation(api: MetadataApi, input: NewVideo): Promise<Video> {
  try {
    return await api.createVideo(input);
  } catch (originalError) {
    if (!isAmbiguousCreateFailure(originalError)) throw originalError;
  }

  // El endpoint de metadata es idempotente para el mismo videoId y el mismo cuerpo.
  try {
    return await api.createVideo(input);
  } catch {
    // El segundo POST también pudo aplicarse sin que llegara la respuesta.
  }

  try {
    const saved = await api.getVideo(input.videoId);
    if (matchesCreateRequest(saved, input)) return saved;
  } catch {
    // Un 404, un 403 o una caída de metadata no autorizan a borrar streaming.
  }

  throw new ApiError(
    503,
    'metadata_result_uncertain',
    `No se pudo confirmar el registro en metadata. Conserva el video ${input.videoId} ` +
      'y no vuelvas a subir el archivo; será necesario reconciliarlo.',
  );
}

interface UploadInput {
  readonly file: File;
  readonly details: VideoDetails;
}

export function useUploadForm() {
  const { streamingApi, metadataApi, queryClient } = useServices();
  const navigate = useNavigate();
  const { data: quota } = useQuota();
  const form = useForm<VideoField>(EMPTY_VIDEO_FIELDS, findVideoErrors);
  const [file, setFile] = useState<File | null>(null);
  const [fileError, setFileError] = useState<string | null>(null);
  const [visibility, setVisibility] = useState<Visibility>('private');
  const [progress, setProgress] = useState(0);
  const uploadAbort = useRef<AbortController | null>(null);

  // Salir de la página no debe dejar una subida corriendo sin que nadie la vea.
  useEffect(() => () => uploadAbort.current?.abort(), []);

  const upload = useMutation({
    mutationFn: async ({ file: video, details }: UploadInput): Promise<Video> => {
      const controller = new AbortController();
      uploadAbort.current = controller;
      setProgress(0);
      const durationSeconds = await readVideoDuration(video);
      const { videoId, sizeBytes } = await streamingApi.uploadVideo(video, {
        onProgress: setProgress,
        signal: controller.signal,
      });
      try {
        return await createMetadataWithReconciliation(metadataApi, {
          ...details,
          videoId,
          durationSeconds,
          sizeBytes,
        });
      } catch (error) {
        // Cuando no sabemos si metadata confirmó el alta, NUNCA borrar el archivo.
        // Borrarlo podría dejar un registro de catálogo que apunta a contenido inexistente.
        if (error instanceof ApiError && error.code === 'metadata_result_uncertain') {
          throw error;
        }
        try {
          await streamingApi.deleteVideo(videoId);
        } catch {
          throw new ApiError(
            503,
            'upload_cleanup_failed',
            `${error instanceof Error ? error.message : 'Error al crear metadata'}. ` +
              `No se pudo deshacer la subida ${videoId}; requiere limpieza manual.`,
          );
        }
        throw error;
      }
    },
    onSuccess: async (video) => {
      await Promise.all([
        queryClient.invalidateQueries({ queryKey: ['videos'] }),
        queryClient.invalidateQueries({ queryKey: [QUOTA_QUERY_KEY] }),
      ]);
      await navigate(`/videos/${video.videoId}`);
    },
  });

  // Si la cuota no se conoce no se sube: el enunciado prohíbe superarla.
  const availableBytes = quota === undefined ? null : quota.quotaBytes - quota.usedBytes;

  return {
    form,
    file,
    fileError,
    visibility,
    progress,
    isUploading: upload.isPending,
    canUpload: availableBytes !== null && availableBytes > 0,
    isQuotaKnown: availableBytes !== null,
    formError:
      upload.error === null || isCancelled(upload.error) ? null : describeUploadError(upload.error),
    changeVisibility: setVisibility,
    changeFile: (selected: File | null) => {
      setFile(selected);
      setFileError(null);
      if (selected !== null && form.values.title === '') {
        form.change('title', selected.name.replace(/\.[^.]+$/, ''));
      }
    },
    cancel: () => {
      uploadAbort.current?.abort();
    },
    submit: (event: SyntheticEvent) => {
      event.preventDefault();
      if (availableBytes === null) {
        return;
      }
      const problem = findFileError(file, availableBytes);
      setFileError(problem);
      if (form.validate() && problem === null && file !== null) {
        upload.mutate({ file, details: toVideoDetails(form.values, visibility) });
      }
    },
  };
}
