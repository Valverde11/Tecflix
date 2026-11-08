import {
  VISIBILITIES,
  type NewVideo,
  type Video,
  type VideoDetails,
  type VideoListQuery,
  type VideoPage,
} from '../types/video';
import type { HttpClient } from './HttpClient';
import { JsonObject } from './JsonObject';

const VIDEOS_PATH = '/api/metadata/videos';

function parseOwnerId(value: string): number {
  if (!/^(0|[1-9]\d*)$/.test(value)) {
    throw new Error('el campo "owner_id" debe ser un entero decimal no negativo');
  }

  const ownerId = Number(value);
  if (!Number.isSafeInteger(ownerId)) {
    throw new Error('el campo "owner_id" excede el rango seguro de JavaScript');
  }
  return ownerId;
}

/** Convierte la respuesta snake_case de metadata al modelo camelCase de la web. */
export function parseVideo(data: unknown): Video {
  const json = new JsonObject(data);
  return {
    videoId: json.string('id'),
    ownerId: parseOwnerId(json.string('owner_id')),
    author: json.string('owner_name'),
    title: json.string('title'),
    description: json.string('description'),
    topic: json.string('topic'),
    tags: json.stringArray('tags'),
    durationSeconds: json.number('duration_seconds'),
    sizeBytes: json.number('size_bytes'),
    visibility: json.oneOf('visibility', VISIBILITIES),
    uploadedAt: json.string('uploaded_at'),
  };
}

function parseVideoPage(data: unknown): VideoPage {
  const json = new JsonObject(data);
  return {
    items: json.array('items').map(parseVideo),
    page: json.number('page'),
    pageSize: json.number('page_size'),
    total: json.number('total'),
  };
}

function videoPath(videoId: string): string {
  return `${VIDEOS_PATH}/${encodeURIComponent(videoId)}`;
}

function toWireSortField(sortBy: VideoListQuery['sortBy']): string {
  return sortBy === 'uploadedAt' ? 'uploaded_at' : sortBy;
}

function toCreateRequest(video: NewVideo) {
  return {
    id: video.videoId,
    title: video.title,
    description: video.description,
    topic: video.topic,
    tags: video.tags,
    duration_seconds: video.durationSeconds,
    size_bytes: video.sizeBytes,
    visibility: video.visibility,
  };
}

export class MetadataApi {
  private readonly http: HttpClient;

  constructor(http: HttpClient) {
    this.http = http;
  }

  /** Orden y paginación los resuelve la API; aquí solo traducimos el contrato HTTP. */
  listVideos(query: VideoListQuery): Promise<VideoPage> {
    return this.http.get(
      VIDEOS_PATH,
      parseVideoPage,
      {
        page: query.page,
        page_size: query.pageSize,
        sort: toWireSortField(query.sortBy),
        order: query.order,
      },
    );
  }

  getVideo(videoId: string): Promise<Video> {
    return this.http.get(videoPath(videoId), parseVideo);
  }

  createVideo(video: NewVideo): Promise<Video> {
    return this.http.post(VIDEOS_PATH, toCreateRequest(video), parseVideo);
  }

  updateVideo(videoId: string, details: VideoDetails): Promise<Video> {
    return this.http.patch(videoPath(videoId), details, parseVideo);
  }

  deleteVideo(videoId: string): Promise<void> {
    return this.http.delete(videoPath(videoId));
  }
}
