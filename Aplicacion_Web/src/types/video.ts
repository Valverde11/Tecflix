export const VIDEO_STATUSES = ['queued', 'processing', 'ready', 'failed'] as const;
export type VideoStatus = (typeof VIDEO_STATUSES)[number];

export const VISIBILITIES = ['public', 'private'] as const;
export type Visibility = (typeof VISIBILITIES)[number];

export type VideoSortField = 'title' | 'author' | 'uploadedAt';
export type SortOrder = 'asc' | 'desc';

export interface Video {
  readonly videoId: string;
  readonly ownerId: number;
  readonly author: string;
  readonly title: string;
  readonly description: string;
  readonly topic: string;
  readonly tags: readonly string[];
  readonly durationSeconds: number;
  readonly sizeBytes: number;
  readonly visibility: Visibility;
  readonly uploadedAt: string;
}

export interface VideoListQuery {
  readonly page: number;
  readonly pageSize: number;
  readonly sortBy: VideoSortField;
  readonly order: SortOrder;
}

export interface VideoPage {
  readonly items: readonly Video[];
  readonly page: number;
  readonly pageSize: number;
  readonly total: number;
}

export interface VideoDetails {
  readonly title: string;
  readonly description: string;
  readonly topic: string;
  readonly tags: readonly string[];
  readonly visibility: Visibility;
}

export interface NewVideo extends VideoDetails {
  readonly videoId: string;
  readonly durationSeconds: number;
  readonly sizeBytes: number;
}
