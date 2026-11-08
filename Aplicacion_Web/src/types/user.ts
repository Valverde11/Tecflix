export interface Quota {
  readonly quotaBytes: number;
  readonly usedBytes: number;
}

export interface User extends Quota {
  readonly id: number;
  readonly username: string;
  readonly email: string;
  readonly createdAt: string;
}

export interface Credentials {
  readonly username: string;
  readonly password: string;
}

export interface NewUser extends Credentials {
  readonly email: string;
}

export interface Session {
  readonly token: string;
  readonly user: User;
}
