import { useMutation } from '@tanstack/react-query';
import { useEffect, useRef, useState, type SyntheticEvent } from 'react';
import { useNavigate } from 'react-router';
import { ApiError } from '../../api/ApiError';
import { UPLOAD_CANCELLED } from '../../api/HttpClient';
import { useForm } from '../../hooks/useForm';
import { useServices } from '../../hooks/useServices';
import type { Video, VideoDetails, Visibility } from '../../types/video';
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
      const videoId = await streamingApi.uploadVideo(video, {
        onProgress: setProgress,
        signal: controller.signal,
      });
      try {
        return await metadataApi.createVideo({
          ...details,
          videoId,
          durationSeconds,
          sizeBytes: video.size,
        });
      } catch (error) {
        // SPEC 3.3: sin metadata nadie vería el video y ocuparía espacio para siempre.
        // Si deshacer también falla, a la persona le sirve más saber el error original.
        await streamingApi.deleteVideo(videoId).catch(() => undefined);
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
