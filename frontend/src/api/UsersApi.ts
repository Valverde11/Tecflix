import type { Credentials, NewUser, Quota, Session, User } from '../types/user';
import type { HttpClient } from './HttpClient';
import { JsonObject } from './JsonObject';

export function parseQuota(data: unknown): Quota {
  const json = new JsonObject(data);
  return { quotaBytes: json.number('quotaBytes'), usedBytes: json.number('usedBytes') };
}

export function parseUser(data: unknown): User {
  const json = new JsonObject(data);
  return {
    ...parseQuota(data),
    id: json.number('id'),
    username: json.string('username'),
    email: json.string('email'),
    createdAt: json.string('createdAt'),
  };
}

function parseLoginResponse(data: unknown): Session {
  const json = new JsonObject(data);
  return { token: json.string('token'), user: parseUser(json.value('user')) };
}

export class UsersApi {
  private readonly http: HttpClient;

  constructor(http: HttpClient) {
    this.http = http;
  }

  register(newUser: NewUser): Promise<User> {
    return this.http.post('/api/users', newUser, parseUser);
  }

  login(credentials: Credentials): Promise<Session> {
    return this.http.post('/api/auth/login', credentials, parseLoginResponse);
  }

  getCurrentUser(): Promise<User> {
    return this.http.get('/api/users/me', parseUser);
  }

  getQuota(userId: number): Promise<Quota> {
    return this.http.get(`/api/users/${String(userId)}/quota`, parseQuota);
  }
}
