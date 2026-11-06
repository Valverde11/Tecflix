import type { ApiConfig } from '../api/apiConfig';
import type { Session, User } from '../types/user';
import type { Video } from '../types/video';

export const TEST_API_CONFIG: ApiConfig = {
  usersApiUrl: 'http://users.test',
  metadataApiUrl: 'http://metadata.test',
  streamingApiUrl: 'http://streaming.test',
};

export const TEST_PASSWORD = 'clave-correcta';
export const TAKEN_USERNAME = 'ocupado';
export const TAKEN_EMAIL = 'ocupado@tec.cr';

const ONE_GIGABYTE = 1_000_000_000;

export const testUser: User = {
  id: 7,
  username: 'ana',
  email: 'ana@tec.cr',
  quotaBytes: ONE_GIGABYTE,
  usedBytes: 0,
  createdAt: '2026-11-03T15:04:05Z',
};

export const testSession: Session = { token: 'token-de-prueba', user: testUser };

export const testVideo: Video = {
  videoId: 'a'.repeat(32),
  ownerId: testUser.id,
  author: testUser.username,
  title: 'Árboles B en disco',
  description: 'Inserción y borrado',
  topic: 'Estructuras de datos',
  tags: ['btree'],
  durationSeconds: 90,
  sizeBytes: 1024,
  visibility: 'public',
  status: 'ready',
  uploadedAt: '2026-11-03T15:04:05Z',
};
