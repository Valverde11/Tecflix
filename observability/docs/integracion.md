# Integración de una API con la observabilidad de Tecflix

Esta guía define los pasos para que cada servicio use la librería compartida `tecflix_telemetry`. No requiere que el Dashboard conozca la lógica interna de usuarios, metadata o streaming.

## 1. Contrato que debe cumplir cada servicio

- Nombre fijo del servicio: `user-service`, `metadata-service` o `streaming-service`.
- `GET /health` responde HTTP `200` con JSON `{"status":"ok"}` cuando el servicio está listo para atender solicitudes.
- El mismo ejecutable debe aceptar la configuración por variables de entorno; no se codifica el hostname ni el endpoint OTLP en el código.
- Cada manejador instrumentado registra el patrón de ruta, por ejemplo `/users/:id`, nunca el ID real (`/users/123`).
- Los estados de respuesta y la duración se registran mediante `Instrument`/`HttpMetrics`.
- Al cerrar el proceso, se llama `Telemetry::ForceFlush()` para intentar exportar las últimas métricas.

## 2. Inicializar la telemetría

En el archivo que construye el servidor HTTP:

```cpp
#include "tecflix/telemetry/telemetry.hpp"
#include "tecflix/telemetry/instrument.hpp"

const auto config = tecflix::telemetry::ConfigFromEnv("user-service");
tecflix::telemetry::Telemetry telemetry(config);
auto& metrics = telemetry.Http();
```

Cambiar el nombre predeterminado por el nombre fijo que corresponda al API. `ConfigFromEnv` sigue permitiendo que `OTEL_SERVICE_NAME` lo sobreescriba si está configurado; por ello, el despliegue debe asegurar que el valor final coincida con el contrato del equipo.

## 3. Envolver las rutas

Ejemplo ilustrativo para un manejador `cpp-httplib`:

```cpp
server.Get("/users/:id",
    tecflix::telemetry::Instrument(
        metrics,
        "/users/:id",
        [](const httplib::Request& req, httplib::Response& res) {
            // Lógica real del servicio.
        }
    )
);
```

Hacer lo mismo para `/health` y las demás rutas. Verificar que las rutas dinámicas usen patrones estables y no valores de usuario, nombres de archivo ni IDs.

## 4. Variables de entorno

| Variable | Ejemplo local | Propósito |
|---|---|---|
| `OTEL_SERVICE_NAME` | `user-service` | Nombre lógico del servicio |
| `SERVICE_INSTANCE_ID` | `local-dev` | ID único para esta instancia |
| `OTEL_EXPORTER_OTLP_ENDPOINT` | `http://localhost:4318` | Endpoint base del Collector OTLP/HTTP |
| `PORT` | `8081` | Puerto HTTP del servicio; nombre según la implementación de cada API |

En despliegues con dos instancias, `SERVICE_INSTANCE_ID` debe ser diferente en cada una. Si el servicio se ejecuta en el host y el Collector en Docker Desktop, el endpoint local suele ser `http://localhost:4318`; si el servicio se ejecuta dentro de la red de Compose, se usa `http://otel-collector:4318`.

## 5. Registrar el endpoint de salud para Blackbox Exporter

El stack local comprueba actualmente `http://host.docker.internal:8090/health` para `sample-service`. Esta ruta es un ejemplo de configuración; no representa las APIs reales.

Cuando una API esté disponible, añadir un target bajo `job_name: service-health` en `observability/stack/prometheus.yaml`, con etiquetas que coincidan exactamente con el contrato del servicio:

```yaml
- targets: ["http://host.docker.internal:8081/health"]
  labels:
    service_name: user-service
    service_instance_id: local-dev
```

Cambiar la URL al puerto real. Para un servicio dentro de Docker Compose, usar el nombre DNS del servicio y el puerto interno, por ejemplo `http://user-service:8081/health`. En Docker Engine sobre Linux podría necesitarse un mapeo `host.docker.internal:host-gateway` para llegar desde un contenedor al host; Docker Desktop en macOS/Windows normalmente proporciona `host.docker.internal`.

La consulta independiente devuelve `probe_success=1` si la respuesta cumple el módulo HTTP configurado (HTTP 200 y JSON con `status: ok`), o `0` si no puede alcanzarlo o no cumple el contrato. La señal no depende de que la API haya exportado métricas. El panel `Salud de servicios (/health)` muestra el último resultado por servicio e instancia.

El chequeo periódico también genera solicitudes HTTP reales al endpoint `/health`; como la ruta está instrumentada, aparecerá en las métricas de solicitudes. Esto es esperado. El intervalo actual de scrape es 15 segundos.

## 6. Validación local

1. Levantar el stack desde `observability/stack/` con `docker compose up -d`.
2. Ejecutar `sample-service` en el host con `PORT=8090 SERVICE_INSTANCE_ID=local-dev`.
3. Esperar al menos 15–30 segundos y consultar en Prometheus `probe_success{job="service-health"}`.
4. Confirmar que la serie de `sample-service / local-dev` vale `1` en Grafana.
5. Detener `sample-service`, esperar el siguiente chequeo y confirmar que pasa a `0`; volver a iniciarlo y comprobar que regresa a `1`.
6. Confirmar que los paneles existentes de tasas, errores y latencia siguen mostrando las métricas HTTP.

No declarar integrada una API hasta que su propio target de salud y sus métricas HTTP se hayan validado con el servicio real.
