# SPEC_INTEGRATION.md — Contrato de integración de Tecflix (borrador V1.0)

**Estado:** borrador técnico para revisión del equipo. No representa todavía una decisión aprobada por los cuatro integrantes.

**Base analizada:** snapshot de `Tecflix.zip` recibido el 8 de noviembre de 2026, rama `V1test`, commit `5b7816a` (`Merge pull request #10 from Valverde11/Aplicación_Web`).

**Objetivo:** definir los contratos HTTP, los responsables de cada dato y el comportamiento esperado entre `user-service`, `metadata-service`, `streaming-service` y la aplicación web. El propósito es evitar que cada componente compile y pase sus pruebas aisladas, pero falle al comunicarse con los demás.

---

## 1. Reglas generales

1. Este documento complementa el enunciado oficial y los SPEC particulares. No reemplaza la lógica interna de cada componente.
2. Los nombres internos de C++ o TypeScript pueden diferir de los nombres JSON. La conversión debe realizarse en el controlador o adaptador API; no se deben cambiar los modelos visuales solo para copiar el formato HTTP.
3. Las rutas y los nombres de campos son sensibles a mayúsculas/minúsculas.
4. Los errores HTTP deben tener un código HTTP correcto y un cuerpo JSON estable.
5. Ningún cliente puede declarar su propio `owner_id` ni decidir si tiene permiso sobre un video. La identidad debe derivarse de un JWT verificado.
6. Los cambios que afecten contratos deben actualizar pruebas de backend, adaptadores de la web, mocks y pruebas de integración.

## 2. Responsabilidad y fuente de verdad

| Dato / responsabilidad | Fuente de verdad | Consumidores |
|---|---|---|
| Identidad, username, email y contraseña | `user-service` | Web, metadata y streaming |
| Firma y vigencia del JWT | `user-service` emite; los servicios validan con el mismo contrato | Web, metadata y streaming |
| Cuota asignada y bytes usados | `user-service` | Web consulta; streaming reserva y libera |
| ID del video | `streaming-service` lo genera | Web y metadata |
| Archivo fuente, segmentos HLS y manifiesto | `streaming-service` | Reproductor web |
| Estado de transcodificación | `streaming-service` | Web |
| Título, descripción, tema, etiquetas, visibilidad, autor y fecha de publicación | `metadata-service` | Web |
| Ordenamiento, filtros y paginación del catálogo | `metadata-service` | Web |
| Métricas, salud y trazas | Cada servicio instrumentado; infraestructura de observabilidad las recopila | Dashboard |

**Regla:** no duplicar un dato de negocio como fuente de verdad en dos servicios. Por ejemplo, metadata no debe inventar el estado de FFmpeg; streaming no debe inventar el título o la visibilidad del catálogo.

## 3. Direcciones y puertos de desarrollo local

| Componente | URL local | Variable del puerto |
|---|---|---|
| `user-service` | `http://localhost:8081` | `PORT=8081` |
| `metadata-service` | `http://localhost:8082` | `PORT=8082` |
| `streaming-service` | `http://localhost:8083` | **Propuesta:** unificar `TECFLIX_PORT` a `PORT` |
| Aplicación web Vite | `http://localhost:5173` | Configuración Vite |
| MongoDB | `mongodb://localhost:27017` | `MONGO_URI` |
| Grafana | `http://localhost:3000` | Docker Compose |
| Prometheus | `http://localhost:9090` | Docker Compose |

La web obtiene las URL mediante `VITE_USERS_API_URL`, `VITE_METADATA_API_URL` y `VITE_STREAMING_API_URL`. Estas variables son públicas y no deben contener secretos.

## 4. Identificadores y tipos

### 4.1 `video_id`

- Lo genera exclusivamente `streaming-service` al recibir una subida.
- Formato: string hexadecimal en minúsculas de exactamente 32 caracteres (`[a-f0-9]{32}`).
- Metadata utiliza ese valor como `id` y como `_id` en MongoDB.
- La web usa internamente `videoId` y convierte el campo HTTP `id` en `videoId` en `MetadataApi` / `StreamingApi`.

### 4.2 `user_id`

- El identificador interno de `user-service` es `std::uint64_t`.
- El JWT lo representa en `sub` como string decimal, tal como ya hace `AuthService::IssueToken`.
- Metadata lo almacena como `owner_id`, string decimal obtenido del `sub` verificado.
- El API actual de usuarios devuelve `id` como entero JSON y la web lo tipa como `number`. Para el tamaño esperado de este proyecto se conserva inicialmente ese formato; el adaptador de metadata debe convertir `owner_id` a número y rechazar valores no seguros si se necesita compararlo como `number`.

## 5. Autenticación y autorización

### 5.1 JWT de usuario

El contrato actual del emisor es:

- Encabezado fijo JWT con `alg=HS256` y `typ=JWT`.
- Firma HMAC-SHA256.
- Claims: `sub` (ID decimal del usuario como string), `username` (string) y `exp` (Unix epoch en segundos).
- Duración actual: 8 horas.
- Todos los servicios que acepten peticiones de usuario deben validar firma, encabezado y expiración. No deben confiar en el contenido antes de validar la firma.

Las peticiones autenticadas del navegador usan:

```http
Authorization: Bearer <jwt>
```

`AUTH_SECRET` debe ser el mismo secreto aleatorio de al menos 32 bytes en `user-service`, `metadata-service` y `streaming-service`. No se expone en Vite ni se sube al repositorio.

**Implementación propuesta:** extraer el código genérico de validación JWT de `Servicios/Servicio_Usuarios` a una librería compartida (`shared/auth` o equivalente), para que las tres APIs validen exactamente el mismo formato. No se recomienda llamar a `user-service` por HTTP en cada petición para validar el token, porque añade dependencia y un punto de fallo.

### 5.2 Token entre servicios

- Header: `X-Service-Token`.
- Se utiliza exclusivamente en solicitudes de servicio a servicio; nunca desde el navegador.
- `user-service` ya valida este header mediante `ServiceTokenVerifier` para las operaciones internas de cuota.
- `SERVICE_SECRET` debe tener al menos 32 bytes y coincidir entre `user-service` y el servicio autorizado a llamar su API de cuota (inicialmente `streaming-service`).

### 5.3 Identidad de metadata

Se deben retirar `X-User-Id` y `X-User-Name` como mecanismo de autenticación. Son manipulables por el cliente y metadata ya documenta que son provisionales e inseguros.

Metadata obtiene:

- `owner_id` desde el claim `sub` del JWT validado.
- `owner_name` desde el claim `username` del JWT validado.

El cuerpo enviado por el navegador no puede establecer ni cambiar `owner_id` ni `owner_name`.

### 5.4 Protección de videos privados

- Un video `public` puede consultarse y reproducirse sin JWT.
- Un video `private` solo puede consultarlo y reproducirlo su propietario autenticado.
- Metadata aplica esta regla para `GET` de detalle/listado.
- Streaming también debe aplicarla a la consulta de estado, manifiesto y segmentos HLS; proteger solo el manifiesto no es suficiente si los segmentos siguen siendo accesibles directamente.
- **Implementación propuesta para streaming:** antes de entregar estado, manifiesto o segmento, comprobar autorización consultando el detalle de metadata y reenviando el `Authorization` recibido. Si metadata no está disponible, streaming debe fallar de forma cerrada y no servir el contenido.
- Shaka ya agrega `Authorization: Bearer <jwt>` a sus solicitudes de red mediante `PlaybackSession`; conservar ese comportamiento.

## 6. Contratos HTTP

### 6.1 `user-service` (puerto 8081)

| Método | Ruta | Autenticación | Resultado esperado |
|---|---|---|---|
| POST | `/api/users` | Pública | Registro; `201` y perfil |
| POST | `/api/auth/login` | Pública | Login; `200` con `{ token, user }` |
| GET | `/api/users/me` | JWT | Perfil actual |
| GET | `/api/users/:id` | JWT | Perfil propio completo o perfil público ajeno |
| PUT | `/api/users/:id` | JWT del propietario | Actualizar username/email/password |
| DELETE | `/api/users/:id` | JWT del propietario | `204` |
| GET | `/api/users/:id/quota` | JWT propietario o `X-Service-Token` | `{ quotaBytes, usedBytes }` |
| POST | `/api/users/:id/quota/usage` | `X-Service-Token` | Modifica uso mediante `{ deltaBytes }` |
| GET | `/health` | Pública | `200 {"status":"ok"}` |

Los nombres `quotaBytes`, `usedBytes`, `createdAt` son camelCase por compatibilidad con el API actual de usuarios y su adaptador web. No deben convertirse parcialmente.

Para `POST /api/users/:id/quota/usage`:
- `deltaBytes > 0`: reserva bytes, rechazando si se excede la cuota.
- `deltaBytes < 0`: libera bytes, rechazando si se intenta liberar más de lo usado.
- `deltaBytes = 0`: no cambia consumo; puede aceptarse como operación neutra.

### 6.2 `metadata-service` (puerto 8082)

Todas las claves JSON de metadata y los parámetros de consulta son `snake_case`, según `SPEC_METADATA.md`.

| Método | Ruta | Autenticación | Resultado esperado |
|---|---|---|---|
| GET | `/health` | Pública | `200 {"status":"ok"}` |
| POST | `/api/metadata/videos` | JWT | Crear metadata; `201` |
| GET | `/api/metadata/videos` | Opcional | Catálogo público; con JWT agrega videos propios |
| GET | `/api/metadata/videos/:id` | Opcional | Detalle público o del propietario |
| PATCH | `/api/metadata/videos/:id` | JWT propietario | Actualizar metadata; `200` |
| DELETE | `/api/metadata/videos/:id` | JWT propietario | Eliminar metadata; `204` |
| GET | `/api/metadata/users/:owner_id/usage` | JWT del propietario | Resumen derivado del catálogo; no sustituye la cuota de `user-service` |

Parámetros de listado:

| Parámetro | Valores / significado |
|---|---|
| `page` | Entero >= 1, default 1 |
| `page_size` | Entero 1–100, default 20 |
| `sort` | `title`, `author`, `uploaded_at`; default `uploaded_at` |
| `order` | `asc` o `desc`; fechas default `desc`, texto default `asc` |
| `mine` | `true` o `false`; requiere JWT si es `true` |
| `tag` | Filtro de etiqueta |
| `q` | Subcadena del título |

Respuesta de listado:

```json
{
  "items": [],
  "page": 1,
  "page_size": 20,
  "total": 0
}
```

Crear metadata: cuerpo de ejemplo (los campos opcionales se omiten si no aplican):

```json
{
  "id": "0123456789abcdef0123456789abcdef",
  "title": "Mi video",
  "description": "Descripción",
  "topic": "Tecnología",
  "tags": ["demo"],
  "duration_seconds": 120,
  "size_bytes": 5000000,
  "visibility": "private"
}
```

`owner_id` y `owner_name` se derivan del JWT y no se aceptan desde el navegador. `size_bytes` no se puede modificar por PATCH.

Respuesta de video de metadata:

```json
{
  "id": "0123456789abcdef0123456789abcdef",
  "owner_id": "123",
  "owner_name": "ana",
  "title": "Mi video",
  "description": "Descripción",
  "topic": "Tecnología",
  "tags": ["demo"],
  "duration_seconds": 120,
  "size_bytes": 5000000,
  "visibility": "private",
  "uploaded_at": "2026-11-08T18:00:00Z",
  "updated_at": "2026-11-08T18:00:00Z",
  "genres": [],
  "cast": []
}
```

Metadata no incluye `status`: el estado de FFmpeg pertenece a streaming.

### 6.3 `streaming-service` (puerto 8083)

| Método | Ruta | Autenticación | Resultado esperado |
|---|---|---|---|
| GET | `/health` | Pública | `200 {"status":"ok"}` |
| POST | `/api/videos` | JWT | Subir `application/octet-stream`; `202` |
| GET | `/api/videos/:id` | JWT si privado; pública si público | `{ "status": "queued\|processing\|ready\|failed" }` |
| GET | `/api/videos/:id/index.m3u8` | JWT si privado; pública si público | Manifiesto HLS |
| GET | `/api/videos/:id/segments/:segment` | JWT si privado; pública si público | Segmento MPEG-TS |
| DELETE | `/api/videos/:id` | JWT propietario | Eliminar archivos/estado y liberar cuota; actualmente falta implementar esta ruta |

La subida debe llevar:

```http
Content-Type: application/octet-stream
Authorization: Bearer <jwt>
```

La respuesta actual es `202 {"id":"<video_id>","status":"queued"}`. **Mejora propuesta:** añadir `size_bytes` calculado por streaming a partir de los bytes realmente recibidos. La web debe usar ese tamaño devuelto por el servidor al crear metadata; no debe confiar en un tamaño enviado únicamente por el navegador.

Todos los endpoints de streaming deben responder correctamente a CORS para el origen configurado y los métodos `GET`, `POST`, `DELETE`, `OPTIONS`, permitiendo `Authorization` y `Content-Type`.

### 6.4 Errores comunes

Formato objetivo para los tres servicios:

```json
{
  "error": "video_not_found",
  "message": "El video no existe"
}
```

- `error`: identificador estable en `snake_case`, útil para lógica de frontend.
- `message`: explicación legible, sin stack trace ni información interna.
- Usar códigos HTTP coherentes: `400`, `401`, `403`, `404`, `409`, `413`, `415`, `500`, `503`.

Streaming actualmente devuelve solamente `error`; debe añadir `message`.

## 7. Adaptación de la aplicación web

Los componentes React mantienen camelCase en sus modelos internos. Los adaptadores API traducen al contrato HTTP.

| Modelo interno web | Campo HTTP metadata |
|---|---|
| `videoId` | `id` |
| `ownerId` | `owner_id` |
| `author` | `owner_name` |
| `durationSeconds` | `duration_seconds` |
| `sizeBytes` | `size_bytes` |
| `uploadedAt` | `uploaded_at` |
| `pageSize` | `page_size` |
| `sortBy: uploadedAt` | `sort=uploaded_at` |

Cambios requeridos en el adaptador:
- `MetadataApi.listVideos()` debe traducir `pageSize` a `page_size` y `sortBy` a `sort`.
- El valor interno `uploadedAt` se convierte en `uploaded_at`.
- `parseVideo()` debe leer las claves snake_case.
- `createVideo()` debe enviar `id`, no `videoId`.
- `updateVideo()` debe usar PATCH, no PUT.
- `Video.status` no debe ser obligatorio en el modelo devuelto por metadata. El detalle obtiene el estado de `streamingApi`; la tarjeta del catálogo no debe inventar un estado si no lo consulta.
- `HttpClient` necesita un método `patch()` o una operación equivalente.
- MSW debe simular el mismo contrato HTTP real (snake_case, PATCH, paginación real, auth y status por streaming). Los mocks actuales no son evidencia de integración porque imitan el contrato camelCase de la web.

## 8. Cuotas: propuesta de comportamiento V1.0

**Decisión propuesta que el equipo debe aceptar:** `size_bytes` representa el tamaño del archivo original recibido por streaming. Es la opción que requiere menos cambios porque la web ya lee el tamaño original y metadata ya usa `size_bytes` para el consumo. Streaming debe verificarlo y devolver el tamaño medido; no se debe confiar en el valor del navegador.

Flujo propuesto:
1. Streaming valida JWT y extrae `sub` como ID del propietario.
2. Streaming lee `Content-Length` y solicita reserva positiva a `user-service` usando `X-Service-Token` y `deltaBytes`.
3. Streaming recibe el cuerpo y comprueba que la cantidad real coincide con `Content-Length`.
4. Si la recepción o el encolado falla, streaming libera la reserva.
5. Si la transcodificación termina correctamente, el consumo permanece reservado.
6. Si falla la transcodificación y se descarta el contenido, streaming libera la reserva.
7. Al eliminar un video, streaming libera exactamente los bytes registrados para ese video.
8. Si metadata no puede crear el registro después de aceptar la subida, la web solicita DELETE a streaming para compensar y liberar el consumo.

Requisitos adicionales:
- Streaming debe guardar de forma persistente `video_id`, `owner_id`, `size_bytes`, `status` y la información necesaria para recuperar el trabajo tras reinicio.
- El registro local de la reserva debe impedir liberar dos veces el mismo video en el flujo normal.
- `POST /api/users/:id/quota/usage` es actualmente un delta no idempotente. Para tolerar reintentos después de fallos de red, se recomienda ampliar el contrato con `operation_id` o implementar reservas identificadas por `video_id`. Esto queda pendiente antes de afirmar idempotencia completa.
- El `GET /api/metadata/users/:owner_id/usage` es un resumen del catálogo y no es la autoridad para autorizar ni reservar cuota.

## 9. Persistencia y recuperación V1.0

### `user-service`
- El archivo binario y el B-tree son la persistencia de usuarios.
- La cuota `used_bytes` es el contador autoritativo; streaming debe modificarlo mediante el endpoint interno autenticado.

### `metadata-service`
- La integración final debe usar MongoDB (`METADATA_ENABLE_MONGO=ON` y el perfil de CMake correspondiente).
- El modo en memoria sirve para pruebas/desarrollo y no demuestra persistencia entre reinicios.
- `MONGO_URI` y `MONGO_DB` deben ser iguales para las instancias que compartan catálogo.

### `streaming-service`
- Actualmente `statuses_` y `jobs_` viven solo en memoria; se pierden al reiniciar.
- V1.0 debe persistir estado y propietario asociado con cada video, y reconstruir el estado a partir de los directorios/archivos al arrancar.
- Si un trabajo quedó `processing` al detenerse el proceso, debe recuperarse como `queued` o marcarse `failed` según la estrategia que se implemente y documente.
- V2.0 requiere almacenamiento de estado y archivos accesible desde todas las instancias; dos directorios locales independientes no constituyen redundancia.

## 10. CORS y variables de entorno

### CORS

Para `http://localhost:5173`:
- `Access-Control-Allow-Origin`: valor exacto de `CORS_ORIGIN`, no `*` cuando se usan credenciales/identidad.
- `Access-Control-Allow-Headers`: `Authorization, Content-Type`.
- `Access-Control-Allow-Methods`: incluir `GET, POST, PUT, PATCH, DELETE, OPTIONS` según las rutas implementadas.
- Responder OPTIONS/preflight con `204` o `200` y los encabezados anteriores.
- Streaming debe habilitar CORS también para manifiestos y segmentos HLS solicitados por Shaka.

### Variables propuestas

| Variable | Servicio | Requerida / default |
|---|---|---|
| `PORT` | Usuarios | default 8081 |
| `PORT` | Metadata | default 8082 |
| `PORT` | Streaming | propuesta default 8083; reemplaza `TECFLIX_PORT` |
| `CORS_ORIGIN` | Usuarios, metadata, streaming | `http://localhost:5173` en desarrollo |
| `AUTH_SECRET` | Usuarios, metadata, streaming | obligatoria; mismo valor, mínimo 32 bytes |
| `SERVICE_SECRET` | Usuarios y streaming | obligatoria; mismo valor, mínimo 32 bytes |
| `DATA_DIR` | Usuarios | default actual `./data` |
| `MONGO_URI` | Metadata | default actual local; conectar Mongo en integración |
| `MONGO_DB` | Metadata | `tecflix_metadata` |
| `TECFLIX_STORAGE` | Streaming | default actual `data` |
| `TECFLIX_FFMPEG` | Streaming | default `ffmpeg` |
| `METADATA_API_URL` | Streaming | propuesta `http://localhost:8082` para autorización de reproducción |
| `OTEL_EXPORTER_OTLP_ENDPOINT` | APIs instrumentadas | endpoint del Collector según entorno |
| `SERVICE_INSTANCE_ID` | APIs instrumentadas | único por proceso/instancia |

Los archivos `.env.example` deben enumerar las variables requeridas, pero mantener secretos vacíos o con marcadores no utilizables. Nunca subir `.env` real.

## 11. Observabilidad y CI (contrato de integración)

- Identidades de recurso: `service.name` con `user-service`, `metadata-service` y `streaming-service`; `service.instance.id` único por instancia.
- Atributos HTTP estables: método, ruta normalizada y código de respuesta. No usar IDs de video/usuario como etiquetas Prometheus.
- Los tres servicios deben instrumentar `GET /health` y sus rutas de negocio.
- Blackbox debe comprobar las tres APIs, no solamente `sample-service`.
- La capa de trazas debe compartir el mismo contexto entre web/API cuando aplique y entre llamadas de servicio.
- CI debe compilar y probar los cuatro componentes en jobs separados y tener un job de integración que levante dependencias (MongoDB, FFmpeg y servicios) para los flujos end-to-end.

## 12. Criterios mínimos de aceptación de integración

1. Crear usuario, iniciar sesión y obtener JWT.
2. Consultar perfil y cuota autenticados.
3. Subir un archivo con JWT; streaming devuelve `202`, un ID válido de 32 hex y el tamaño medido.
4. Crear metadata usando `id`, campos snake_case y JWT; el dueño se deriva del token.
5. Consultar catálogo con paginación y ordenamiento; web traduce camelCase interno a query snake_case.
6. Video público reproducible sin sesión; video privado reproducible solo por el dueño.
7. Consultar estado `queued`, `processing`, `ready` o `failed` desde streaming.
8. Eliminar un video; se eliminan metadata y archivos y se libera una única vez la cuota.
9. Reiniciar servicios y comprobar que usuarios, metadata, estados y archivos requeridos sobreviven según el contrato.
10. Ejecutar el flujo con MSW desactivado y los servicios reales; no aceptar mocks como evidencia del contrato real.
11. Confirmar que las métricas y el Dashboard identifican las tres APIs y sus instancias.

## 13. Decisiones pendientes de aprobación del equipo

1. **Cuota:** confirmar que se contabilizará el tamaño del archivo original y no la suma física de segmentos HLS. Esta es la propuesta de este borrador.
2. **ID de usuario en el API público:** mantener por ahora el `id` numérico actual de user-service y `owner_id` decimal como string en metadata, o migrar todas las respuestas a string. No mezclar formatos de manera implícita.
3. **Estado en el catálogo:** propuesta: mantener el estado como responsabilidad exclusiva de streaming y obtenerlo desde streaming en detalle; no persistir un `status` duplicado en MongoDB. Para mostrar estado en tarjetas se necesitaría un endpoint batch o una consulta por video; no hacer N solicitudes sin medir el impacto.
4. **Autenticación compartida:** confirmar extracción de la validación JWT a una librería común y distribución de `AUTH_SECRET` idéntico.
5. **Eliminación:** confirmar el orden coordinado streaming → metadata en la web, con operaciones idempotentes y reintentos; la autorización de streaming para borrar debe usar el propietario persistido del upload, porque el registro de metadata puede ser eliminado después.
6. **V2.0:** definir almacenamiento compartido/replicado de archivos y persistencia de la cola/estado de transcodificación antes de desplegar dos instancias.

---

**Regla de trabajo:** no cambiar simultáneamente el contrato en backend y frontend sin actualizar pruebas. Cada paso de integración debe cerrar con: build del componente afectado, tests unitarios y una prueba HTTP real con `curl` o una prueba de integración automatizada.
