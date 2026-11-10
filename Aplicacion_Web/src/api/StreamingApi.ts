import { VIDEO_STATUSES, type VideoStatus } from '../types/video';
import type { HttpClient, UploadOptions } from './HttpClient';
import { JsonObject } from './JsonObject';

const VIDEOS_PATH = '/api/videos';

function parseStatus(data: unknown): VideoStatus {
  return new JsonObject(data).oneOf('status', VIDEO_STATUSES);
}

export interface UploadedVideo {
  readonly videoId: string;
  readonly sizeBytes: number;
}

function parseUploadedVideo(data: unknown): UploadedVideo {
  const json = new JsonObject(data);
  const videoId = json.string('id');
  const sizeBytes = json.number('size_bytes');
  if (!/^[a-f0-9]{32}$/.test(videoId)) {
    throw new Error('el campo "id" debe ser un identificador hexadecimal de 32 caracteres');
  }
  if (!Number.isSafeInteger(sizeBytes) || sizeBytes <= 0) {
    throw new Error('el campo "size_bytes" debe ser un entero positivo seguro');
  }
  return { videoId, sizeBytes };
}

function videoPath(videoId: string): string {
  return `${VIDEOS_PATH}/${encodeURIComponent(videoId)}`;
}

export class StreamingApi {
  private readonly http: HttpClient;
  private readonly baseUrl: string;

  constructor(http: HttpClient, baseUrl: string) {
    this.http = http;
    this.baseUrl = baseUrl;
  }

  /** Devuelve el ID y el tamaño original medido por streaming, no por el navegador. */
  uploadVideo(file: Blob, options: UploadOptions): Promise<UploadedVideo> {
    return this.http.upload(VIDEOS_PATH, file, parseUploadedVideo, options);
  }

  getStatus(videoId: string): Promise<VideoStatus> {
    return this.http.get(videoPath(videoId), parseStatus);
  }

  /** Shaka Player descarga el manifiesto por su cuenta, así que solo necesita la URL. */
  getManifestUrl(videoId: string): string {
    return `${this.baseUrl}${videoPath(videoId)}/index.m3u8`;
  }

  deleteVideo(videoId: string): Promise<void> {
    return this.http.delete(videoPath(videoId));
  }
}
