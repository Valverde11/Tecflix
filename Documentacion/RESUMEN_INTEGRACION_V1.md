# Tecflix V1 — Resumen de integración para el equipo

**Fecha:** 13 de noviembre de 2026  
**Rama de integración:** `V1test`  
**Estado:** pruebas locales completas aprobadas según ejecución reportada por el equipo; el resultado de GitHub Actions y el end-to-end real deben confirmarse por separado.

## Qué cambió

### Contrato compartido

- **Usuarios (`user-service`, puerto 8081):** conserva identificadores numéricos, emite JWT y es autoridad sobre el consumo de cuota. Se utiliza un validador JWT compartido entre las tres APIs, con el mismo `AUTH_SECRET`.
- **Metadata (`metadata-service`, puerto 8082):** usa `owner_id` como cadena decimal, es autoridad sobre título, descripción, etiquetas, visibilidad y catálogo. En el despliegue local integrado se ejecutó con MongoDB. **No guarda el estado de transcodificación.**
- **Streaming (`streaming-service`, puerto 8083):** genera `video_id` de 32 caracteres hexadecimales, es autoridad sobre archivos, segmentos HLS, transcodificación y estado (`queued`, `processing`, `ready`, `failed`). En la cuota se cuentan **los bytes realmente recibidos del archivo original**; los segmentos/manifiestos HLS no se suman a la cuota lógica.
- **Web (`Aplicacion_Web`, puerto 5173):** coordina las llamadas a las APIs sin inventar propietarios ni estados; se comprobó el funcionamiento de los mecanismos de recuperación ante fallos parciales.

### Idempotencia, recuperaciones y eliminación

- Usuarios registra operaciones de cuota mediante identificadores estables para evitar cobros o liberaciones duplicados durante reintentos.
- Streaming conserva estado de subida y recupera operaciones de reserva/liberación tras reinicios; los trabajos pendientes se reintentan conforme a sus registros persistidos.
- Si streaming acepta un video pero metadata no está disponible, la web almacena **metadata pendiente** en `localStorage`, conserva el identificador original y reintenta la creación sin volver a subir el archivo. Se validó bloqueando temporalmente el acceso a metadata y recargando la página.
- Si la eliminación queda incompleta, la web conserva **eliminaciones pendientes** y las reintenta. Se validó bloqueando streaming, recargando la página y restableciendo la conexión.
- Streaming puede responder **HTTP 202 `deletion_pending`**: no equivale a eliminación completada; la web no debe tratarlo como un 204 definitivo. La liberación de cuota ocurre una sola vez.
- **Atención al contrato:** el `SPEC_INTEGRATION.md` archivado define eliminación `streaming → metadata`, pero las pruebas anteriores de recuperación de la web se realizaron con un flujo que eliminaba metadata primero. Debemos resolver y documentar esta divergencia antes de afirmar que el contrato de eliminación está completamente alineado. Asimismo, el `Idempotency-Key` del POST de subida documentado en el SPEC no está validado por la evidencia disponible.

### Observabilidad real

```text
user-service (8081) ───────┐
metadata-service (8082) ───┼─ OTLP/HTTP → OpenTelemetry Collector → Prometheus → Grafana
streaming-service (8083) ──┘
             ↑
Blackbox Exporter comprueba /health de las tres APIs por separado
```

- Se configuró `TECFLIX_REAL_TELEMETRY=ON` en las compilaciones locales de las tres APIs y se verificó su exportación.
- Las etiquetas de observabilidad incluyen `service_name`, `service_instance_id`, método HTTP, código HTTP y ruta normalizada; por ejemplo, `/api/videos/:id` en lugar de un ID específico.
- Prometheus registró solicitudes de los tres servicios y `probe_success{job="service-health"}=1` para todos.
- Grafana mostró RPS, latencia promedio, p95/p99, códigos de respuesta y 5xx. Se corrigió la escala porcentual del panel de 5xx y su consulta para representar `0 %` cuando hay tráfico pero no hay 5xx.
- Se comprobaron rutas de negocio y respuestas `200` y `404`. Los percentiles de rutas con tráfico escaso podían devolver `NaN` y pasaron a dar valores al generar más solicitudes. El p95 cercano a 23,75 ms es **estimación del histograma**, no tiempo exacto medido.

## Qué se probó

- `bash tecflix_v1_verification.sh`: la ejecución final se reportó con todos los bloques aprobados y sin omisiones, **después** de introducir la instrumentación de metadata.
- Pruebas web: lint, compilación TypeScript/Vite y Vitest.
- C++: CTest para streaming, usuarios, metadata y observabilidad.
- Prueba manual HTTP real: `GET /health` en los tres servicios; rutas no saludables como `GET /api/auth/login` y `GET /api/videos/:id` registraron correctamente respuestas 404.
- Pruebas manuales de recuperación: metadata pendiente y eliminación pendiente mediante interrupciones controladas en Chrome DevTools.

> Estos resultados son **locales**. No equivalen a que los runners limpios de GitHub Actions hayan pasado ni prueban por sí solos un E2E real de subida, transcodificación y reproducción con MongoDB/FFmpeg en CI.

## Próximos pasos y responsables

| Frente | Trabajo restante |
|---|---|
| Dashboard | Mantener consultas y documentación actualizadas; las tres APIs ya emiten métricas reales |
| Web | Revisar el contrato definitivo de orden de DELETE y el comportamiento de reintentos cuando se pierde la respuesta del POST inicial |
| Streaming | Verificar el contrato de subida (`Idempotency-Key`) frente a la especificación y la recuperación de estados persistidos |
| Usuarios/metadata | Revisar idempotencia y que los contratos de operaciones de cuota / MongoDB reflejen exactamente la implementación |
| Equipo | Abrir PR, ejecutar CI en GitHub y hacer un E2E con servicios reales, MongoDB y FFmpeg; documentar resultados |

## Arranque y comprobación rápida

- Web: `cd Aplicacion_Web && npm ci && npm run lint && npm run build && npm test`.
- C++: ejecutar `bash tecflix_v1_verification.sh` desde la raíz, con las compilaciones creadas previamente.
- Observabilidad: `cd observability/stack && docker compose up -d`.
- Prometheus: `sum by (service_name) (http_server_request_duration_seconds_count)`.
- Salud: `probe_success{job="service-health"}`.
- Variables de cada proceso: `OTEL_SERVICE_NAME`, `SERVICE_INSTANCE_ID`, `OTEL_EXPORTER_OTLP_ENDPOINT=http://localhost:4318`; también las variables funcionales de cada API.

## Precauciones para subir a Git

No subir `.env`, secretos, `data/`, `quarantine/`, archivos `segment_*.ts`, manifiestos/transcodificaciones generadas, bases MongoDB locales, `node_modules/`, `build/`, `dist/`, ni volúmenes Docker. Verificar con `git status --short`, `git check-ignore -v` y `git ls-files` antes de confirmar el commit. Añadir `quarantine/` al `.gitignore` si no está excluido.
