# Tecflix — Aplicación web

Interfaz de Tecflix: Vite, React y TypeScript. Las URLs de las APIs se configuran en `.env` (copiar `.env.example`).

Mientras metadata y streaming no estén integradas, `VITE_MOCK_VIDEO_APIS=true` las simula en desarrollo; la API de usuarios sigue siendo la real. Los videos del catálogo son del usuario con id 7, así que "Administrar" solo aparece en los que subas. En este modo no hay video real que reproducir.

```sh
npm install
npm run dev            # http://localhost:5173
npm test               # pruebas
npm run coverage       # pruebas con umbral de cobertura
npm run lint           # ESLint
npm run format:check   # Prettier
npm run build          # verifica tipos y genera dist/
```
