# Tecflix
Sistema de streaming de video "Tecflix" con arquitectura distribuida. Desarrollado en C++ para el curso de Algoritmos y Estructuras de Datos II.

## Observabilidad local

La primera etapa del dashboard usa Prometheus para almacenar métricas y Grafana para mostrarlas. OpenTelemetry Collector recibe las métricas OTLP que emitan los servicios y las expone a Prometheus.

```text
Servicios C++ --OTLP--> OpenTelemetry Collector --scrape--> Prometheus <-- Grafana
```

### Requisitos

- Docker Desktop (o Docker Engine) con Docker Compose v2.

### Iniciar

Desde la raíz del repositorio:

```sh
docker compose up -d
```

Abre estas direcciones en tu navegador:

- Grafana: http://localhost:3000 (usuario y contraseña iniciales: `admin` / `admin`; cambia la contraseña al iniciar sesión).
- Prometheus: http://localhost:9090

Comprueba que Prometheus ve sus dos targets en http://localhost:9090/targets: `otel-collector` y `otel-metrics`. En Grafana, abre **Dashboards → Tecflix → Tecflix - System Overview**.

Los paneles de disponibilidad del collector deberían mostrar datos enseguida. Los paneles HTTP permanecerán vacíos hasta que los servicios C++ envíen métricas. Los puertos OTLP `4317` (gRPC) y `4318` (HTTP) están publicados solo en la máquina local.

Para detener los contenedores sin borrar los datos:

```sh
docker compose down
```

Para eliminar también los datos locales de Prometheus y Grafana:

```sh
docker compose down -v
```
