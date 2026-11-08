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

Si Grafana dice que `Tecflix - System Overview` ya existe con el mismo UID al intentar importarlo, no lo importes de nuevo: el dashboard se provisiona automáticamente desde el archivo local. Ábrelo directamente en http://localhost:3000/d/tecflix-system/tecflix-system-overview o búscalo desde **Dashboards** en la carpeta **Tecflix**.

### Ejemplo C++: contador y latencia

El ejemplo de [main.cpp](./examples/cpp-metrics/main.cpp) simula solicitudes `GET` y registra dos métricas:

- `tecflix.http.server.requests`: un **contador**, que aumenta una vez por solicitud.
- `http.server.request.duration`: un **histograma** en segundos, que guarda cuánto tardó cada solicitud y permite calcular percentiles como p95. Sus buckets explícitos van de 25 ms a 1 s, con límites más detallados donde caen las duraciones del ejemplo.

El programa simula 40 solicitudes: cada décima responde con HTTP 500 y las demás con HTTP 200. No es todavía un servidor web; sirve para aprender y comprobar el recorrido de las métricas.

Necesitas CMake, un compilador C++17 y Git. La primera configuración descarga OpenTelemetry C++ v1.20.0.

```sh
cmake -S examples/cpp-metrics -B build/cpp-metrics
cmake --build build/cpp-metrics --target tecflix_metrics_demo
./build/cpp-metrics/tecflix_metrics_demo
```

La configuración genera `build/cpp-metrics/compile_commands.json` para que el editor pueda encontrar los encabezados de OpenTelemetry. Si VS Code aún subraya los `#include`, configura la extensión C/C++ para usar ese archivo de compilación o selecciona CMake como proveedor de configuración IntelliSense.

La advertencia de CMake acerca de `CMP0092` proviene del `CMakeLists.txt` de OpenTelemetry descargado en `build/`; es una advertencia de política obsoleta del proyecto externo, no un error de compilación. No edites ese archivo generado dentro de `build/`.

Primero deja corriendo el dashboard con `docker compose up -d`. Después ejecuta el ejemplo. En Prometheus, puedes probar:

```promql
tecflix_http_server_requests_total
```

Esta consulta directa confirma que el contador llegó. Las consultas con `rate()` requieren varias muestras dentro del rango de tiempo; como Prometheus recopila cada 15 segundos, espera al menos 30 segundos después de iniciar el demo antes de consultar tasas o percentiles:

```promql
sum by (service_name) (rate(tecflix_http_server_requests_total[1m]))
```

```promql
histogram_quantile(
  0.95,
  sum by (le, service_name) (rate(http_server_request_duration_seconds_bucket[5m]))
)
```

En Grafana abre **Dashboards → Tecflix → Tecflix - System Overview**. El programa termina después de emitir las solicitudes; `ForceFlush()` intenta exportar las últimas mediciones antes de salir. Al destruirse el `MeterProvider`, OpenTelemetry apaga el lector y el exportador.

El p95 es una estimación calculada dentro del bucket que contiene el percentil, no una medición exacta de una solicitud individual. Para este simulador debería quedar cerca de 0.11–0.125 segundos. Si cambias la carga para producir latencias muy diferentes, ajusta también los límites del histograma.

### Cómo leer el ejemplo

1. `StartMetrics()` conecta el SDK de OpenTelemetry con el Collector por OTLP/HTTP (`localhost:4318/v1/metrics`).
2. El recurso `service.name=tecflix-cpp-demo` identifica qué servicio produjo las métricas.
3. `RequestMetrics` crea los instrumentos: el contador y el histograma.
4. En el ciclo, `steady_clock` mide el tiempo transcurrido; después `RecordRequest()` incrementa el contador y registra la duración junto con el método y el código HTTP.
5. El Collector expone esas mediciones a Prometheus, y Grafana consulta Prometheus para dibujar los paneles.

Para usar el mismo patrón en una API real, llama `RecordRequest` al terminar de procesar cada petición y sustituye la espera simulada por tu lógica real de endpoint.

Para detener los contenedores sin borrar los datos:

```sh
docker compose down
```

Para eliminar también los datos locales de Prometheus y Grafana:

```sh
docker compose down -v
```
