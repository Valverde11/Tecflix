# Tecflix V1 — Acta de validación final

**Fecha:** noviembre 2026. **Rama:** `V1test`. **Entorno:** desarrollo local macOS y GitHub Actions.

## Evidencia verificada hasta ahora

| Control | Estado | Evidencia |
|---|---|---|
| Web: lint, build, pruebas | Aprobado en la regresión local y CI funcional | Resultados comunicados por el equipo |
| Streaming: CTest | Aprobado local y CI | Regresión y cuatro jobs verdes |
| Usuarios: CTest | Aprobado local y CI | Regresión y cuatro jobs verdes |
| Metadata: CTest en modo memoria en CI | Aprobado | Cuatro jobs verdes |
| Observabilidad: pruebas locales | Aprobado sin omisiones | Última ejecución del script local |
| Tres APIs con métricas reales en Prometheus/Grafana | Aprobado localmente | RPS, p95, rutas, HTTP 200/404 y probes |
| CI con telemetría ON | **Pendiente** | Ejecutar nuevo job matricial en GitHub |
| E2E completo con MongoDB y FFmpeg reales | **Pendiente** | Completar procedimiento siguiente |

## Prueba E2E real — ejecutar de forma controlada

1. Iniciar MongoDB local y los tres binarios configurados con la misma clave JWT, cuotas habilitadas, metadata con MongoDB y streaming con FFmpeg. Comprobar `/health` en 8081/8082/8083. No cambiar la base de datos si contiene datos importantes; usar un usuario de prueba y un video no confidencial.
2. Iniciar observabilidad: `cd observability/stack && docker compose up -d`. En las terminales de cada servicio configurar `TECFLIX_REAL_TELEMETRY=ON` **durante la compilación** y las variables de runtime `OTEL_SERVICE_NAME`, `SERVICE_INSTANCE_ID`, `OTEL_EXPORTER_OTLP_ENDPOINT`.
3. Iniciar la web: `cd Aplicacion_Web && npm ci && npm run dev`, con `VITE_MOCK_VIDEO_APIS=false` y los tres endpoints locales.
4. Crear un usuario temporal, entrar, anotar cuota inicial y subir un video pequeño. Confirmar que la respuesta de streaming incluye un `video_id` y `size_bytes` igual a los bytes originales recibidos (no al tamaño HLS).
5. Verificar metadata con el mismo video ID, propietario numérico convertido en cadena decimal, y ausencia de estado persistido de transcodificación en MongoDB. Esperar `ready`, reproducir playlist y segmentos desde la web.
6. Eliminar el video desde la web; comprobar que desaparece del catálogo, que streaming lo devuelve como no encontrado cuando corresponda y que la cuota se libera exactamente una vez. **Si la eliminación retorna HTTP 202**, validar su recuperación hasta completarse, sin tratar 202 como 204.
7. Interrumpir temporalmente **solo el acceso web a metadata** durante una subida de prueba: confirmar registro pendiente y reconciliación sin subir el archivo de nuevo. Repetir con una eliminación bloqueando streaming y verificar reintento. Estos escenarios ya se ensayaron manualmente; registrar ahora la evidencia final si se repiten.
8. Consultar Prometheus: `sum by (service_name, http_route, http_response_status_code) (http_server_request_duration_seconds_count)`; comprobar rutas de negocio de las tres APIs. Capturar una pantalla del dashboard con RPS, latencia y respuestas; no depender exclusivamente de `/health`.

**Registro de evidencias:** guardar fecha, SHA del commit (`git rev-parse --short HEAD`), estado CI, capturas sin tokens, video usado, tamaño original, cuota antes/después y resultado por servicio. No guardar credenciales, JWT ni `.env` en capturas o reportes.

## Validación no destructiva de Git

Desde la raíz: `bash scripts/check_commit_hygiene.sh`. El script revisa nombres de archivos ya rastreados por Git y no borra, mueve ni imprime contenido de archivos. Antes del commit, usar también `git status --short`, `git diff --check`, `git diff --cached --stat` y `git check-ignore -v quarantine/ data/ .env`.

Si hay archivos prohibidos previamente rastreados, retirar del índice con `git rm --cached` (no `rm` del archivo local) y comprobar qué se está cambiando antes de confirmar. Nunca usar `git add -f` para `.env`, videos o datos de ejecución.

## Límites no resueltos

- Los cuatro jobs existentes validan metadata sin MongoDB real. El E2E con MongoDB y FFmpeg debe probarse por separado.
- El orden de eliminación del SPEC y el flujo web recuperable observado difieren: decidir y documentar el contrato definitivo.
- No se ha demostrado el uso de `Idempotency-Key` del POST de subida al perder la respuesta original; distinguirlo de la idempotencia de las reservas de cuota.
- No se ha comprobado exportación de trazas distribuidas: en V1 se validaron **métricas** HTTP reales.
