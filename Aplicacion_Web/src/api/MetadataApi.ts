import {
  VIDEO_STATUSES,
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

export function parseVideo(data: unknown): Video {
  const json = new JsonObject(data);
  return {
    videoId: json.string('videoId'),
    ownerId: json.number('ownerId'),
    author: json.string('author'),
    title: json.string('title'),
    description: json.string('description'),
    topic: json.string('topic'),
    tags: json.stringArray('tags'),
    durationSeconds: json.number('durationSeconds'),
    sizeBytes: json.number('sizeBytes'),
    visibility: json.oneOf('visibility', VISIBILITIES),
    status: json.oneOf('status', VIDEO_STATUSES),
    uploadedAt: json.string('uploadedAt'),
  };
}

function parseVideoPage(data: unknown): VideoPage {
  const json = new JsonObject(data);
  return {
    items: json.array('items').map(parseVideo),
    page: json.number('page'),
    pageSize: json.number('pageSize'),
    total: json.number('total'),
  };
}

function videoPath(videoId: string): string {
  return `${VIDEOS_PATH}/${encodeURIComponent(videoId)}`;
}

export class MetadataApi {
  private readonly http: HttpClient;

  constructor(http: HttpClient) {
    this.http = http;
  }

  /** Orden y paginación los resuelve la API (requisito del enunciado), nunca la web. */
  listVideos(query: VideoListQuery): Promise<VideoPage> {
    return this.http.get(VIDEOS_PATH, parseVideoPage, { ...query });
  }

  getVideo(videoId: string): Promise<Video> {
    return this.http.get(videoPath(videoId), parseVideo);
  }

  createVideo(video: NewVideo): Promise<Video> {
    return this.http.post(VIDEOS_PATH, video, parseVideo);
  }

  updateVideo(videoId: string, details: VideoDetails): Promise<Video> {
    return this.http.put(videoPath(videoId), details, parseVideo);
  }

  deleteVideo(videoId: string): Promise<void> {
    return this.http.delete(videoPath(videoId));
  }
}
