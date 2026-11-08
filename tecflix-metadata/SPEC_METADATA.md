# SPEC_METADATA.md — API de metadata (`metadata-service`)

Complementa a `SPEC.md` y cubre **solo** `services/metadata-service/`. Si algo contradice a `SPEC.md`, manda `SPEC.md`.
**Estado:** versión provisional para probar la integración. Última revisión: 2026-11-04.

## 1. Alcance

Catálogo de metadata de videos (SPEC §3.3): crea, consulta, lista (ordenada y paginada), modifica y elimina la metadata, respeta público/privado y calcula el almacenamiento usado por usuario (para la cuota). Datos en **MongoDB**. No sirve video (eso es `streaming-service`) ni guarda usuarios (eso es `user-service`).

## 2. Contrato HTTP

Puerto por defecto **8082**. Claves JSON en `snake_case`; fechas ISO 8601 UTC (`2026-11-04T15:30:00Z`); duración en segundos.

**Identidad provisional:** cabeceras `X-User-Id` (obligatoria para escribir) y `X-User-Name` (opcional). Es **inseguro** (cualquiera puede mentir) y se reemplaza cuando `user-service` defina la autenticación.

| Método y ruta | Qué hace | Respuestas |
|---|---|---|
| `GET /health` | Salud (contrato del dashboard) | `200 {"status":"ok"}` |
| `POST /api/metadata/videos` | Crea metadata de un video ya subido a streaming | `201` + `Location`, `400`, `401`, `409`, `415` |
| `GET /api/metadata/videos` | Lista paginada | `200`, `400`, `401` (solo con `mine=true` sin usuario) |
| `GET /api/metadata/videos/:id` | Detalle | `200`, `404` |
| `PATCH /api/metadata/videos/:id` | Modifica campos | `200`, `400`, `401`, `403`, `404` |
| `DELETE /api/metadata/videos/:id` | Elimina | `204`, `401`, `403`, `404` |
| `GET /api/metadata/users/:owner_id/usage` | `{"owner_id","video_count","used_bytes"}` | `200`, `401`, `403` |

**Lista** (`GET /api/metadata/videos`): `page` (≥1, por defecto 1), `page_size` (1–100, por defecto 20), `sort` = `title` \| `author` \| `uploaded_at` (por defecto `uploaded_at`), `order` = `asc` \| `desc` (por defecto: `desc` para fechas, `asc` para texto), `mine=true`, `tag=…`, `q=…` (subcadena del título). Respuesta: `{"items":[…],"page":1,"page_size":20,"total":123}`. Sin usuario solo se ven los públicos; con usuario, los públicos más los propios.

**Crear** (cuerpo): `id` (obligatorio, el `video_id` de 32 hex minúscula que devolvió streaming), `title` (obligatorio, ≤200), `description`, `topic`, `tags`, `duration_seconds`, `size_bytes`, `visibility` (`public`/`private`, por defecto **private**), `genres`, `release_year`, `cast`. El dueño sale de `X-User-Id`, **no** del cuerpo. Campos desconocidos se rechazan (`unknown_field`).

**Modificar** (cuerpo): cualquiera de `title`, `description`, `topic`, `tags`, `duration_seconds`, `visibility`, `genres`, `release_year`, `cast`. No editables: `id`, `owner_id`, `owner_name`, `uploaded_at`, `updated_at` y `size_bytes` (si se pudiera bajar, se evadiría la cuota).

**Errores:** `{"error":"codigo_snake_case","message":"texto"}`. Códigos: `missing_user_id` (401), `invalid_user_header`, `invalid_json`, `validation_failed`, `unknown_field`, `field_not_editable`, `empty_patch`, `invalid_query` (400), `not_owner` (403), `video_not_found` (404), `video_already_exists` (409), `use_application_json` (415), `internal_error` (500, sin detalles internos). Un video **privado ajeno** responde `404`, no `403`, para no revelar que existe; uno **público ajeno** que se intenta modificar responde `403`.

## 3. Base de datos MongoDB

Base `tecflix_metadata` (variable `MONGO_DB`), colección `videos`. Un documento por video:

```json
{
  "_id": "0123456789abcdef0123456789abcdef",
  "owner_id": "u1", "owner_name": "Ana",
  "title": "…", "description": "", "topic": "", "tags": ["demo"],
  "duration_seconds": 120, "size_bytes": 5000,
  "visibility": "public",
  "uploaded_at": "2026-11-04T15:30:00Z", "updated_at": "2026-11-04T15:30:00Z",
  "genres": [], "cast": [], "release_year": 2020
}
```

Decisiones (y qué problema de integración evitan):
- **`_id` = `video_id` de streaming** (string de 32 hex). Un `ObjectId` tiene 24 hex y nunca coincidiría; así la unión entre APIs es una igualdad de strings, sin tablas de equivalencia.
- **`owner_id` es una referencia**, no se copian usuarios: `user-service` es la única fuente de verdad. `owner_name` es una copia solo para poder ordenar por autor; se desactualiza si el usuario cambia de nombre (pendiente de decidir con el grupo).
- **`size_bytes` guardado:** la cuota se calcula como `sum(size_bytes)` por `owner_id` (agregación). Streaming borra el original tras transcodificar y no informa tamaños, así que lo aporta quien crea la metadata.
- **Fechas como texto ISO 8601 UTC:** ordenan igual que fechas reales y evitan depender de los tipos `b_date` del driver, cuya API cambia entre mongocxx 3.x y 4.x.
- **Paginación con `skip/limit`:** simple y suficiente para el proyecto; con millones de videos convendría paginar por cursor.

Índices (se crean al arrancar):
`{owner_id:1, uploaded_at:-1}` (mis videos, cuota) · `{visibility:1, uploaded_at:-1}` (públicos) · `{tags:1}` · `{title:1}` y `{owner_name:1}` con collation `es` strength 2 (orden por título/autor sin distinguir mayúsculas ni acentos). Con collation, el filtro por `owner_id`/`visibility` no puede usar su índice sin collation: es aceptable a esta escala. La búsqueda `q` usa regex con el texto escapado (no hay índice de texto todavía).

## 4. Arquitectura del código (`services/metadata-service/`)

Mismo estilo que `user-service`: `include/tecflix/metadata/*.hpp`, `src/*.cpp`, `tests/`, namespace `tecflix::metadata`, métodos `PascalCase`, 2 espacios, `-Werror`.

- `Config`/`Environment`: variables `PORT` (8082), `CORS_ORIGIN` (`http://localhost:5173`), `MONGO_URI` (`mongodb://localhost:27017`), `MONGO_DB` (`tecflix_metadata`). Un puerto inválido detiene el arranque.
- `Video` (modelo) · `video_json` (JSON ↔ modelo, estricto) · `video_validation` · `time_util` (ISO 8601 portable).
- `VideoRepository` (interfaz) con `InMemoryVideoRepository` (pruebas y desarrollo) y `MongoVideoRepository` (producción).
- `CatalogService`: reglas de negocio (propiedad, visibilidad, validación); lanza `CatalogError`.
- `VideoController` + `HealthController` + `ApiServer` (httplib, CORS de un solo origen, cuerpo máximo 1 MB).
- `repository_factory`: único `#ifdef` del proyecto (`METADATA_WITH_MONGO`).

**Telemetría (SPEC §4): pendiente.** Los patrones de ruta ya están como constantes (`/api/metadata/videos/:id`) y los manejadores tienen la firma de `tecflix::telemetry::HttpHandler`, de modo que integrar `Instrument(metrics, kVideoRoute, …)` es envolver cada ruta en `VideoController::RegisterRoutes`. Falta que la librería de observability se pueda enlazar desde un servicio (ver §6).

## 5. Compilar y ejecutar

**Sin MongoDB (en memoria; sirve para integrar y probar en cualquier máquina):**

```sh
cd services/metadata-service
cmake --preset default
cmake --build --preset default
ctest --preset default
build/metadata-service          # build\metadata-service.exe en Windows
```

**Con MongoDB (Windows, CLion):**
1. Instalar MongoDB Community Server (servicio local, puerto 27017).
2. Instalar vcpkg **por separado** (no el que trae Visual Studio): `git clone https://github.com/microsoft/vcpkg C:\vcpkg`, ejecutar `C:\vcpkg\bootstrap-vcpkg.bat` y definir la variable de entorno `VCPKG_ROOT=C:\vcpkg`.
3. En CLion: abrir `services/metadata-service`, activar el perfil del preset `mongo` (Settings → Build → CMake). La primera configuración compila mongocxx y tarda varios minutos.
4. Ejecutar `metadata-service`. Si MongoDB no responde, falla al arrancar; añada `?serverSelectionTimeoutMS=3000` a `MONGO_URI` para no esperar 30 s.

**Prueba rápida (PowerShell):**

```powershell
$h = @{ "X-User-Id" = "u1"; "X-User-Name" = "Ana" }
$body = @{ id = "0123456789abcdef0123456789abcdef"; title = "Mi primer video"; visibility = "public" } | ConvertTo-Json
Invoke-RestMethod -Method Post -Uri http://localhost:8082/api/metadata/videos -Headers $h -ContentType "application/json" -Body $body
Invoke-RestMethod "http://localhost:8082/api/metadata/videos?sort=title&order=asc&page=1&page_size=10" -Headers $h
```

## 6. Qué está verificado y qué no

Verificado (Linux, GCC 13, 2026-11-04): compila con `-Wall -Wextra -Wpedantic -Werror`; **75 pruebas pasan**; cobertura de líneas **99 %** de la lógica (sin `main`, la fábrica y el repositorio Mongo); el ejecutable en memoria responde correctamente a `curl`. `MongoVideoRepository` **compila y enlaza** con mongocxx 4.6.2 y la conversión JSON ↔ BSON se comprobó con el driver real.

**No verificado:** `MongoVideoRepository` contra un `mongod` real (no había servidor disponible); Windows/MSVC; mongocxx vía vcpkg; el repositorio Mongo no tiene pruebas unitarias (necesitaría un servidor), por lo que cuenta fuera del 90 % de cobertura exigido para V2.0.

## 7. Riesgos de integración conocidos

1. **cpp-httplib:** `streaming` y `user-service` usan **0.62.0**; observability y su SPEC proponen **0.18.3**. Unificar en 0.62.0.
2. **Target `httplib::httplib`** definido por tres CMake distintos: choca si se junta todo con `add_subdirectory`. Aquí cada dependencia se declara solo si no existe.
3. **Variables de entorno:** `user-service` y metadata usan `PORT`; `streaming` usa `TECFLIX_PORT`.
4. **Puertos:** users 8081, metadata 8082, streaming debería pasar de 8080 a 8083 (su `launch.json` ya lo usa).
5. **Rutas:** streaming ocupa `/api/videos/…`; metadata usa `/api/metadata/…` para que un balanceador pueda enrutar por prefijo.
6. **Estructura:** el SPEC pide `services/<nombre>/`; streaming está en la raíz del repositorio (renombrarlo genera conflictos).
7. **Librería de telemetría** no enlazable desde un servicio: su `CMakeLists.txt` usa `FetchContent_Declare` sin `include(FetchContent)` y depende del padre.
8. **Autenticación, formato de `user_id`, cuota, cálculo de la duración y borrado coordinado con streaming** (no tiene `DELETE`): sin definir.
9. **`SPEC.md` y `.gitignore` compartidos:** cada rama los edita; esta entrega no los toca (usa este archivo y `services/metadata-service/.gitignore`).

## 8. Líneas propuestas para "Agent memory" de `SPEC.md` (añadir al final, una por una)

- metadata-service usa `PORT` (8082), prefijo `/api/metadata`, `_id` de Mongo = `video_id` de streaming (32 hex) y errores `{"error","message"}`.
- Identidad provisional por cabeceras `X-User-Id`/`X-User-Name` hasta que user-service defina autenticación.
- El repositorio de datos es una interfaz con implementación en memoria y otra MongoDB (mongocxx vía vcpkg) para poder compilar sin el driver.
