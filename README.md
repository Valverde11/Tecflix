# Tecflix V1

Sistema de streaming distribuido desarrollado para Algoritmos y Estructuras de Datos II. Integra aplicación web React/TypeScript, tres APIs C++, MongoDB (metadata), procesamiento HLS con FFmpeg y observabilidad mediante OpenTelemetry, Prometheus y Grafana.

## Componentes

| Componente | Puerto local | Función |
|---|---:|---|
| Web Vite | 5173 | Registro, catálogo, subida, reproducción y administración |
| `user-service` | 8081 | Autenticación JWT, usuarios y cuotas persistentes |
| `metadata-service` | 8082 | Catálogo, visibilidad y metadata; en integración local utiliza MongoDB |
| `streaming-service` | 8083 | Archivo original, transcodificación, segmentos HLS y estado del video |
| Grafana / Prometheus | 3000 / 9090 | Visualización y consulta de métricas |
| OTLP HTTP | 4318 | Recepción de métricas del Collector |

Los IDs de usuario son numéricos; `owner_id` en metadata es una cadena decimal. El video utiliza un ID hexadecimal de 32 caracteres. El estado de transcodificación pertenece a streaming, **no se duplica en MongoDB**. Las cuotas cuentan los bytes realmente recibidos del archivo original, no los segmentos HLS.

## Preparación local

- C++17, CMake, Node.js 24 y npm, Docker Compose, FFmpeg y MongoDB para el modo de integración real.
- Crear las variables privadas siguiendo los `.env.example`. No versionar `.env`, `AUTH_SECRET` ni `SERVICE_SECRET`.
- Compartir `AUTH_SECRET` entre las APIs para validar los JWT. La configuración de cada servicio se describe en `Documentacion/TECFLIX_COMANDOS_Y_PRUEBAS.md` y `Documentacion/RESUMEN_INTEGRACION_V1.md`.
- Para conectar la web a servicios reales: `VITE_MOCK_VIDEO_APIS=false`, con las URLs de las APIs 8081, 8082 y 8083.

**Observabilidad:** iniciar desde la carpeta de Compose, no desde la raíz:

```bash
cd observability/stack
docker compose up -d
docker compose ps
```

Para compilar cada API con métricas OTLP reales, configurar `-DTECFLIX_REAL_TELEMETRY=ON` en su compilación CMake y arrancar con las variables adecuadas:

```bash
export OTEL_EXPORTER_OTLP_ENDPOINT=http://localhost:4318
export SERVICE_INSTANCE_ID=local-dev
# Definir en cada terminal, respectivamente:
# OTEL_SERVICE_NAME=user-service | metadata-service | streaming-service
```

Si se configura `TECFLIX_REAL_TELEMETRY=OFF`, el ejecutable funcional puede compilarse sin la librería OpenTelemetry real; esta opción es usada por los cuatro jobs de regresión funcional en CI.

Grafana: http://localhost:3000 — dashboard `Tecflix - System Overview` de la carpeta Tecflix. Prometheus: http://localhost:9090. La contraseña inicial de Grafana puede haber cambiado; no almacenar credenciales en el repositorio. El dashboard se aprovisiona desde `observability/stack/grafana/dashboards/`.

## Verificación

```bash
cd Aplicacion_Web
npm ci
npm run lint
npm run build
npm test
```

Desde la raíz del repositorio, una vez compilados los servicios:

```bash
bash tecflix_v1_verification.sh
```

Ese script **no crea las compilaciones faltantes** y falla si omite bloques. GitHub Actions ejecuta cuatro jobs de regresión (web, streaming, usuarios y metadata en modo memoria) y un job matricial adicional que compila las tres APIs con telemetría real habilitada. La compilación de telemetría no sustituye las pruebas E2E ni el modo MongoDB de metadata.

Consultas útiles en Prometheus:

```promql
probe_success{job="service-health"}
```

```promql
sum by (service_name) (http_server_request_duration_seconds_count)
```

```promql
sum by (service_name) (rate(http_server_request_duration_seconds_count[5m]))
```

`probe_success=1` significa que se puede consultar `/health`; **no garantiza** que la API exporte métricas. Para verificar OTLP, comprobar que `http_server_request_duration_seconds_count` aparece en los tres servicios.

Los percentiles de histogramas son estimaciones dependientes de buckets: con solicitudes `/health` muy breves pueden aparecer valores p95 alrededor de 23,75 ms pese a promedios muy inferiores. Para evaluar desempeño real deben generarse rutas funcionales, no solamente `/health`.

## Material de entrega

- `Documentacion/SPEC_INTEGRATION.md`: contrato previsto y diferencias identificadas respecto al flujo actual.
- `Documentacion/SPEC_DASHBOARD.md`: diseño de observabilidad y validación real.
- `Documentacion/RESUMEN_INTEGRACION_V1.md`: resumen para los cuatro integrantes.
- `Documentacion/VALIDACION_FINAL_V1.md`: pruebas E2E, seguridad de Git y evidencias por recoger.
- `Aplicacion_Web/README.md`: configuración y pruebas de la interfaz.
- `observability/docs/integracion.md`: detalles de instrumentación.

**Pendiente de acuerdo del equipo:** revisar la divergencia del orden de eliminación `streaming → metadata` frente al flujo de recuperación observado y confirmar si el contrato exige `Idempotency-Key` en el POST de subida. La idempotencia de cuotas **no equivale** a idempotencia de la subida HTTP.
