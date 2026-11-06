# Tecflix — Aplicación web

Interfaz de Tecflix: Vite, React y TypeScript. Las URLs de las APIs se configuran en `.env` (copiar `.env.example`).

Mientras metadata no esté integrada, `VITE_MOCK_METADATA=true` la simula en desarrollo con un catálogo de prueba; la API de usuarios sigue siendo la real. Los videos del catálogo pertenecen al usuario con id 7, así que "Administrar" no aparece con cuentas nuevas.

```sh
npm install
npm run dev            # http://localhost:5173
npm test               # pruebas
npm run coverage       # pruebas con umbral de cobertura
npm run lint           # ESLint
npm run format:check   # Prettier
npm run build          # verifica tipos y genera dist/
```
