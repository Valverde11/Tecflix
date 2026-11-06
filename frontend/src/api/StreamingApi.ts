import { VIDEO_STATUSES, type VideoStatus } from '../types/video';
import type { HttpClient } from './HttpClient';
import { JsonObject } from './JsonObject';

const VIDEOS_PATH = '/api/videos';

function parseStatus(data: unknown): VideoStatus {
  return new JsonObject(data).oneOf('status', VIDEO_STATUSES);
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
