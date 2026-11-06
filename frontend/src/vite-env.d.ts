/// <reference types="vite/client" />
/// <reference types="msw/vite/client" />

interface ImportMetaEnv {
  readonly VITE_USERS_API_URL: string;
  readonly VITE_METADATA_API_URL: string;
  readonly VITE_STREAMING_API_URL: string;
  readonly VITE_MOCK_METADATA?: string;
}

interface ImportMeta {
  readonly env: ImportMetaEnv;
}
