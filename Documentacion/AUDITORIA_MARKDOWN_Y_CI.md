# Auditoría de Markdown y GitHub Actions — Tecflix V1

## Alcance y procedencia

Se revisó el archivo `Copia de Tecflix(1).zip` aportado anteriormente, que contiene documentación y un workflow, **más los cambios y resultados reportados después**. Esta copia es anterior a la instrumentación real de metadata: no representa necesariamente el estado actual de la rama. En la copia, `tecflix-metadata` aparece como `tecflix-metadata.zip` anidado, no como el árbol fuente requerido por CI.

## Hallazgos con prioridad

| Archivo | Hallazgo | Acción recomendada |
|---|---|---|
| `README.md` raíz | Describe observabilidad como primera etapa y se centra en el simulador; su instrucción `docker compose up -d` desde raíz no coincide con la comprobada en `observability/stack` | Añadir sección de arquitectura V1 real y apuntar a `observability/stack` |
| `SPEC_INTEGRATION.md` | Declara `borrador` y menciona DELETE de streaming como pendiente, persistencia/operaciones idempotentes como pendientes y propone renombrar `TECFLIX_PORT` a `PORT` | Actualizar estado de cada requisito con evidencia; **no cambiar código por una propuesta antigua** |
| `SPEC_INTEGRATION.md` | Define eliminación streaming → metadata, mientras la recuperación web validada previamente usó metadata → streaming | Resolver con compañeros y tests antes de fijar contrato definitivo |
| `SPEC_INTEGRATION.md` | Exige `Idempotency-Key` al subir; no se ha verificado esa protección para respuesta inicial perdida | Marcar explícitamente como brecha pendiente o implementar/testear |
| `SPEC_DASHBOARD.md` | La sección de hoja de ruta y estado se redactó para `sample-service` | Añadir estado: tres APIs reales exportan OTLP; tablas de evidencias y notas sobre buckets |
| `observability/docs/integracion.md` | Propone envolver manualmente rutas y describe `sample-service` como único health target; ya no corresponde al servidor instrumentado | Explicar `HttpMetricsTracker` con logger + pre-routing y los tres targets actuales |
| `Aplicacion_Web/README.md` | Documenta comandos, variables y mocks; poca información del mecanismo actual de metadata y eliminación pendientes | Agregar `localStorage` de recuperación, semántica HTTP 202 y procedimiento de prueba |
| `TECFLIX_COMANDOS_Y_PRUEBAS.md` | Es la guía práctica más cercana a la ejecución actual | Integrar apartado de CI, comandos de telemetría `ON` y comprobación en Grafana |
| `.github/workflows/observability.yml` | Solo observa cambios en observabilidad y `SPEC_DASHBOARD.md` | Conservarlo y agregar workflow separado para web/streaming/usuarios/metadata; revisar cobertura en runner real |
| `.gitignore` raíz | Excluye datos y builds, pero no aparece `quarantine/` | Añadir `/quarantine/` y revisar los archivos ya rastreados |

## Diferencias importantes entre CI local y GitHub

- `tecflix_v1_verification.sh` omite bloques cuando faltan builds. CI debe **configurar y compilar desde cero**.
- El nuevo workflow compila metadata con `METADATA_ENABLE_MONGO=OFF`: garantiza pruebas unitarias y compilación sin driver, pero **no acredita el modo MongoDB**. Esa validación debe añadirse mediante job específico o documentarse como manual.
- El nuevo workflow usa `TECFLIX_REAL_TELEMETRY=OFF` para un smoke test más ligero. La compilación real `ON` se debe comprobar por separado: el workflow de observabilidad cubre la librería, **no garantiza que se enlacen las tres APIs con telemetría ON**.
- El workflow existente usa `actions/checkout@v7` y `actions/upload-artifact@v7`. Mantenerlo por ahora; revisar en el runner si `llvm-cov` encuentra sus binarios y exporta el JSON esperado.
- Las dependencias de CMake se obtienen con `FetchContent`: los runners de GitHub necesitan acceso a red y pueden tardar bastante en una primera ejecución.

## Checklist antes de abrir PR

1. Confirmar que `tecflix-metadata/services/metadata-service/CMakeLists.txt` **existe como archivos versionados**, no solo dentro de un ZIP.
2. Copiar el workflow nuevo a `.github/workflows/tecflix-v1-ci.yml` sin sobrescribir `observability.yml`.
3. Añadir `/quarantine/` a `.gitignore` y comprobar que ningún video o secreto está rastreado.
4. Revisar `git diff --check`, `git status --short` y los cambios en `.md`.
5. Abrir PR hacia la rama de integración; comprobar los cuatro jobs nuevos y el job previo de observabilidad.
6. En caso de error, usar logs de CI; no asumir que compilar localmente garantiza el resultado del runner.
7. Programar un E2E real de usuarios + MongoDB + streaming/FFmpeg + web, distinto del E2E con MSW.

## Snippet sugerido para README principal

```md
## Tecflix V1 — Arquitectura integrada

- **Web (5173):** interfaz React/TypeScript, autenticación, catálogo, carga, reproducción y recuperación de operaciones.
- **Usuarios (8081):** cuentas, JWT, cuota autoritativa e idempotencia de ajustes de cuota.
- **Metadata (8082):** catálogo, propiedades y visibilidad, respaldado por MongoDB en la integración real.
- **Streaming (8083):** subida de archivo original, estado de transcodificación, archivos HLS, eliminación y reserva/liberación de cuota.
- **Observabilidad:** las tres APIs exportan métricas por OTLP al Collector; Prometheus las almacena y Grafana las presenta.

El stack local se inicia con `cd observability/stack && docker compose up -d`.
Para comprobar: `probe_success{job="service-health"}` y `sum by (service_name) (http_server_request_duration_seconds_count)` en Prometheus.
La guía completa de integración y sus limitaciones está en `Documentacion/RESUMEN_INTEGRACION_V1.md`.
```

## Snippet para `.gitignore`

```gitignore
/quarantine/
```
