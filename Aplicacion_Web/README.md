# Tecflix — Aplicación web

Interfaz de Tecflix hecha con Vite, React 19 y TypeScript en modo estricto. Permite registrarse, iniciar sesión, ver la lista de videos (propios y públicos), reproducirlos con Shaka Player (HLS), subirlos con barra de progreso, editar su metadata, cambiarlos a público o privado y borrarlos. La subida se bloquea al alcanzar la cuota.

La web **no ordena ni pagina** los videos: se los pide así a la API de metadata. Los contratos que consume están en `SPEC.md`, y la explicación del código para la defensa en `Documentacion/Codigo_Web.md`.

## Requisitos

- Node.js 24 (probado con 24.22) y npm.
- La API de usuarios corriendo (por defecto en `http://localhost:8081`). Instrucciones en `Servicios/Servicio_Usuarios/` (rama `API_Usuarios`).
- Las APIs de metadata y streaming, o el modo simulado (ver abajo).

## Configuración

Copiar `.env.example` a `.env` y ajustar:

| Variable                 | Por defecto en el ejemplo | Para qué                                                   |
| ------------------------ | ------------------------- | ---------------------------------------------------------- |
| `VITE_USERS_API_URL`     | `http://localhost:8081`   | Registro, login, cuota                                     |
| `VITE_METADATA_API_URL`  | `http://localhost:8082`   | Lista, detalle y edición de videos                         |
| `VITE_STREAMING_API_URL` | `http://localhost:8083`   | Subida, estado de transcodificación, HLS, borrado          |
| `VITE_MOCK_VIDEO_APIS`   | `false`                   | `true` simula metadata y streaming (solo en `npm run dev`) |

Las tres URLs son obligatorias y deben ser `http` o `https`. Si falta alguna, la app no arranca y el error dice cuál. Cada API debe permitir CORS desde el origen de la web (`http://localhost:5173`), con las cabeceras `Authorization` y `Content-Type`.

### Modo simulado

Con `VITE_MOCK_VIDEO_APIS=true`, MSW intercepta en el navegador las peticiones a metadata y streaming. Así se puede recorrer la app sin esas APIs. La API de usuarios sigue siendo la real, porque el login y la cuota no se simulan. Los videos del catálogo simulado son del usuario con id 7, así que "Administrar" solo aparece en los que subas. En este modo no hay video real que reproducir. El simulador no entra en el build de producción.

## Comandos

```sh
npm install
npm run dev            # servidor de desarrollo en http://localhost:5173
npm test               # pruebas (Vitest + Testing Library + MSW)
npm run test:watch     # pruebas en modo observación
npm run coverage       # pruebas con informe en coverage/; falla si baja del 90 %
npm run lint           # ESLint sin advertencias permitidas
npm run format:check   # Prettier (npm run format corrige)
npm run build          # verifica tipos y genera dist/
npm run preview        # sirve dist/ para revisarlo
npm run e2e            # flujo de Playwright en Chromium (ver abajo)
```

## Prueba de punta a punta (Playwright)

`e2e/main-flow.spec.ts` recorre registro → login → subir → reproducir en Chromium real. La primera vez hay que descargar el navegador con `npx playwright install chromium`.

Playwright arranca la web con `vite dev`. La API de usuarios siempre es la real: si ya corre en `E2E_USERS_API_URL`, la reutiliza; si se da `E2E_USERS_SERVICE_COMMAND`, la arranca él mismo con una carpeta de datos temporal y secretos nuevos, sin tocar los datos de desarrollo. En Windows:

```sh
E2E_USERS_SERVICE_COMMAND='..\Servicios\Servicio_Usuarios\build\user-service.exe' npm run e2e
```

| Variable                                        | Por defecto             | Para qué                                           |
| ----------------------------------------------- | ----------------------- | -------------------------------------------------- |
| `E2E_USERS_SERVICE_COMMAND`                     | —                       | Comando que arranca la API de usuarios             |
| `E2E_USERS_API_URL`                             | `http://localhost:8081` | Dónde está (o se arranca) la API de usuarios       |
| `E2E_MOCK_VIDEO_APIS`                           | `true`                  | `false` usa metadata y streaming reales            |
| `E2E_METADATA_API_URL`, `E2E_STREAMING_API_URL` | `:8082`, `:8083`        | URLs reales de esas APIs                           |
| `E2E_VIDEO_FILE`                                | —                       | Video real a subir (necesario con las APIs reales) |

Con los simuladores, el último paso solo comprueba que se abre el reproductor, porque no hay HLS real. Con `E2E_MOCK_VIDEO_APIS=false`, además exige que el video cargue imagen sin errores. Si la API de usuarios es un build Debug, el registro y el login tardan varios segundos por el hash; Release es mucho más rápido.

## Rutas

| Ruta                  | Acceso          | Pantalla                                   |
| --------------------- | --------------- | ------------------------------------------ |
| `/`                   | —               | Redirige a `/login`                        |
| `/login`, `/register` | Solo sin sesión | Inicio de sesión y registro                |
| `/videos?page=&sort=` | Con sesión      | Lista paginada; el estado vive en la URL   |
| `/videos/:id`         | Con sesión      | Reproductor, metadata y acciones del dueño |
| `/upload`             | Con sesión      | Subida con cuota y progreso                |

## Estructura

```
src/
  api/          único lugar con HTTP: HttpClient, UsersApi, MetadataApi, StreamingApi, SessionStore
  features/     auth, videos, player, upload (pantallas, componentes y hooks de cada parte)
  components/   piezas compartidas (AppLayout, TextField)
  hooks/        useForm, useServices
  types/        tipos del dominio
  test/         servidor MSW, handlers y renderApp para las pruebas
```

La sesión se guarda en `sessionStorage`, así que se cierra al cerrar la pestaña. Un 401 en una petición con token cierra la sesión y lleva al login.
