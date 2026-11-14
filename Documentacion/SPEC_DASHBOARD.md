# SPEC_DASHBOARD.md — Dashboard del sistema (observabilidad)

Este archivo complementa a `SPEC.md` y cubre **solo** el módulo `observability/`. Si algo contradice a `SPEC.md`, manda `SPEC.md`. Antes de trabajar en este módulo, un agente lee `SPEC.md` (sección "Agent memory" incluida) y este archivo.

**Responsable:** la persona del equipo que desarrolla el dashboard. **Última revisión:** 2026-10-26.

## 1. Alcance

Entregar lo que pide `SPEC.md` §3.5 y valen 10 puntos de la rúbrica, y además la base de evidencia para la versión 2.0 (optimizaciones, pruebas de carga y paper):

1. Una **librería C++ reutilizable** (`tecflix_telemetry`) que las tres APIs usan para emitir métricas y traces.
2. Un **stack local** (OpenTelemetry Collector, Prometheus, Grafana) levantado con Docker Compose.
3. Un **dashboard** que muestre errores, salud, desempeño por componente y estadísticas generales.
4. Pruebas unitarias del módulo (≥ 90 % de cobertura) y su integración en CI.

Fuera de alcance: la lógica de negocio de las APIs y la aplicación web. El módulo es **independiente** del código de los demás integrantes; la integración ocurre a través de la librería y del contrato de `SPEC.md` §4.

## 2. Arquitectura

```
API C++ ──(OTLP/HTTP, métricas)──► OpenTelemetry Collector ──► Prometheus ◄── Grafana
 (librería tecflix_telemetry)              :4317 / :4318            :9464       :9090 / :3000

Prometheus ──(HTTP probe)──► Blackbox Exporter ──(GET /health)──► servicios
```

- Las APIs **empujan** métricas por OTLP/HTTP al Collector; Prometheus las **jala** del Collector cada 15 s; Grafana consulta Prometheus.
- Las métricas de aplicación siguen pasando por OTLP y el Collector. Para la señal de vida se usa un camino independiente: Prometheus consulta Blackbox Exporter, que hace GET al `/health` de cada servicio.
- Consecuencia importante: Prometheus solo ve al Collector por el pipeline de métricas; eso por sí solo no demuestra que una API esté viva. El probe HTTP es una señal separada.

## 3. Estructura del módulo

```
observability/
├── CMakeLists.txt           # proyecto propio; incluye pruebas (GoogleTest) y httplib
├── CMakePresets.json        # preset "windows" (Visual Studio 2022 + vcpkg)
├── vcpkg.json               # dependencias para Windows: protobuf, curl
├── telemetry/               # librería tecflix_telemetry
│   ├── CMakeLists.txt
│   ├── include/tecflix/telemetry/{config.hpp, telemetry.hpp}
│   └── src/{config.cpp, telemetry.cpp, telemetry_noop.cpp}
├── sample-service/          # API de juguete para generar tráfico (src/main.cpp)
├── tests/                   # telemetry_test.cpp, telemetry_export_test.cpp, instrument_test.cpp
├── docs/integracion.md      # contrato para integrar cada API
├── scripts/check_coverage.py # umbral de cobertura para CI
└── stack/                   # Compose, Collector, Prometheus, Blackbox Exporter y Grafana
```

## 4. Stack local (Docker)

| Servicio | Imagen (versión fija) | Puerto en tu máquina |
|---|---|---|
| OpenTelemetry Collector | `otel/opentelemetry-collector-contrib:0.120.0` | 4317 (gRPC), 4318 (HTTP) |
| Prometheus | `prom/prometheus:v3.2.1` | 9090 |
| Grafana | `grafana/grafana:11.5.2` | 3000 (usuario inicial `admin`/`admin`: cambiarlo) |
| Blackbox Exporter | `prom/blackbox-exporter:v0.29.1` | 9115, solo dentro de la red Compose |

- Los puertos se publican solo en `127.0.0.1`. **Para la versión 2.0, con APIs en otra máquina, hay que abrirlos** (ver §12).
- Prometheus recopila métricas propias de sí mismo, del Collector (`otel-collector:8888`), del exporter OTLP (`otel-metrics:9464`) y del Blackbox Exporter (`blackbox-exporter:9115`). El intervalo general es 15 s.
- El job `service-health` consulta `/health` por medio de Blackbox Exporter. En el entorno local, el target inicial es `http://host.docker.internal:8090/health`, correspondiente al `sample-service` ejecutado en el host y etiquetado como `sample-service / local-dev`.
- La configuración Blackbox valida HTTP 200 y un cuerpo que contenga `"status":"ok"`. `probe_success=1` indica que la comprobación pasó; `0` indica que no pasó. La comprobación no depende de que la API exporte métricas.
- El Collector convierte los atributos del recurso (`service.name`, `service.instance.id`) en etiquetas de Prometheus. Aún solo tiene un pipeline de **métricas**; las trazas siguen pendientes.
- Grafana se aprovisiona desde archivos: datasource con uid `prometheus` y dashboard `tecflix-system` (carpeta Tecflix, archivo `grafana/dashboards/tecflix-system.json`, montado de solo lectura). Los cambios se hacen en el archivo, no en la interfaz.
- Comandos (desde `observability/stack/`): `docker compose up -d`, `docker compose ps`, `docker compose down` (conserva datos), `docker compose down -v` (borra datos).

## 5. Librería `tecflix_telemetry`

### 5.1 Interfaz pública (namespace `tecflix::telemetry`)

- `Config { service_name, instance_id, otlp_endpoint }` y `ConfigFromEnv(default_service_name)`.
- `MetricsURL(config)`: quita las barras finales del endpoint y agrega `/v1/metrics`.
- `HttpMetrics` (clase abstracta): `Record(method, route, status_code, duration_seconds)`.
- `NoOpHttpMetrics`: versión muda, para pruebas unitarias de otros componentes sin Collector.
- `Telemetry(const Config&)`: crea el proveedor de métricas; `Http()` devuelve el `HttpMetrics` real; `ForceFlush()` devuelve `true` si todo se envió. No es copiable. El destructor apaga el exportador. Usa el patrón *pimpl*, así los encabezados no arrastran OpenTelemetry.

### 5.2 Configuración (variables de entorno)

| Variable | Valor por defecto |
|---|---|
| `OTEL_SERVICE_NAME` | el nombre que pasa el código a `ConfigFromEnv` |
| `SERVICE_INSTANCE_ID` | `instance-1` (ver riesgo en §11) |
| `OTEL_EXPORTER_OTLP_ENDPOINT` | `http://localhost:4318` |

Una variable vacía se trata igual que una no definida. El SDK envía por defecto protobuf binario; con `OTEL_EXPORTER_OTLP_METRICS_PROTOCOL=http/json` envía JSON (lo usa la prueba de exportación).

### 5.3 Métricas que emite

- Histograma `http.server.request.duration`, unidad segundos, **límites de los buckets**: 0.025, 0.05, 0.075, 0.1, 0.125, 0.15, 0.2, 0.3, 0.5, 1.0.
- Atributos: `http.request.method`, `http.route`, `http.response.status_code`. Recurso: `service.name`, `service.instance.id`.
- Contador `tecflix.http.server.requests` (redundante: el `_count` del histograma ya cuenta solicitudes; el dashboard no lo usa).
- Nombres en Prometheus: `http_server_request_duration_seconds_{bucket,count,sum}` con etiquetas `service_name`, `service_instance_id`, `http_route`, `http_request_method`, `http_response_status_code` (además de `job`, `instance`, `exported_job`, `exported_instance`, `telemetry_sdk_*`). **Agrupar siempre por `service_instance_id`**, no por `instance` (que es el Collector).
- Intervalo de exportación: 1 s, tiempo límite 0.5 s.

### 5.4 Reglas de diseño (no romper)

- El nombre del medidor (`tecflix.telemetry`, versión `1.0.0`) y el de la "vista" que define los buckets **deben coincidir**. Si divergen, los buckets vuelven a los predeterminados del SDK (0, 5, 10, 25…) **sin error**, y el p95 sale absurdo (se midió 4.75 s para solicitudes de 30–115 ms). Hay una prueba que lo protege.
- La ruta se registra con su patrón (`/videos/:id`), nunca con la URL real (explosión de cardinalidad).
- `Record` se espera seguro entre hilos (diseño de los instrumentos síncronos del SDK); verificar con carga real en modo ON.
- La librería no depende de ninguna biblioteca HTTP: el envoltorio que mide cada manejador (`Instrument`) vive en el servicio.

### 5.5 Opciones de CMake

| Opción | Por defecto | Efecto |
|---|---|---|
| `TECFLIX_ENABLE_TELEMETRY` | `ON` | `OFF` compila `telemetry_noop.cpp` (sin OpenTelemetry): rápido, sin Protobuf ni curl |
| `TECFLIX_COVERAGE` | `OFF` | Instrumenta con las banderas de cobertura de clang (macOS) |

Dependencia: OpenTelemetry C++ **v1.20.0** (`FetchContent`). Su exportador OTLP/HTTP necesita Protobuf, libcurl, nlohmann-json y zlib. En macOS: `brew install protobuf`. En Windows: vcpkg (ver §10). El `CMakeLists.txt` de la librería guarda y restaura `BUILD_TESTING` alrededor de la descarga, para no desactivar las pruebas del resto del proyecto.

## 6. Servicio de ejemplo (`sample-service`)

Servidor HTTP con `cpp-httplib` v0.18.3 que genera tráfico realista:

- `GET /health` → `200 {"status":"ok"}`.
- `GET /videos/:id` → tarda 20–200 ms al azar y falla con `500` cerca del 10 % de las veces.
- Variable `PORT` (por defecto 8090). Se detiene limpio con Ctrl+C: envía las últimas métricas y termina.
- Es el banco de pruebas del dashboard; no es una API de negocio.

## 7. Dashboard de Grafana (`tecflix-system`)

Variables: **Servicio** e **Instancia** (múltiples, con "All"). Las consultas HTTP filtran por ambas y las consultas de tasa/latencia usan `$__rate_interval`; la señal de salud usa `probe_success`.

| Panel | Qué muestra |
|---|---|
| Collector / exportador `UP` | Prometheus ve al Collector y a su exportador |
| Tasa de errores 5xx | % de respuestas 5xx (verde < 1 %, naranja ≤ 5 %, rojo > 5 %) |
| Latencia p95 | p95 global (verde < 0.2 s, naranja ≤ 0.5 s, rojo > 0.5 s) |
| Solicitudes por segundo por instancia | Tráfico de cada servicio e instancia |
| Latencia p95 por servicio y ruta | Qué ruta es lenta |
| Tasa de errores 5xx por servicio | Un servicio sin errores aparece en 0 %, no desaparece |
| Solicitudes por código de estado | Mezcla de 200, 404, 500... apilada |
| Promedio vs p95 vs p99 | Muestra por qué el promedio engaña |
| Rutas ordenadas por p95 | Tabla para decidir qué optimizar primero |
| Salud de servicios (`/health`) | Último resultado del probe HTTP por servicio e instancia; independiente de OTLP |
| Blackbox Exporter | Estado del proceso que ejecuta las comprobaciones de salud |

Convenciones PromQL: `sum(rate(...)) or vector(0)` para que "sin errores" sea 0 y no "sin datos"; `or 0 * sum by (...)(...)` para conservar series con valor cero; el p95 es una **estimación** calculada dentro del bucket (p. ej. con 32 solicitudes ≤ 100 ms de 40 y 40 ≤ 125 ms: 0.1 + 0.025 × (38 − 32)/(40 − 32) = 118.75 ms).

**Validación del dashboard de métricas (estado anterior):** las 12 consultas de métricas se verificaron con `promtool` 3.2.1 y 6 con datos sintéticos. El nuevo job `service-health`, la consulta `probe_success` y el nuevo panel deben validarse en runtime tras actualizar Compose; no se considera validado solo por editar los archivos.

## 8. Pruebas y cobertura

- `telemetry_test.cpp` contiene 8 pruebas; `instrument_test.cpp`, 10; `telemetry_export_test.cpp`, 1 (solo con `TECFLIX_ENABLE_TELEMETRY=ON`). La ejecución local reportada más reciente fue **19/19 tests passed** con telemetría habilitada.
- Cobertura reportada por el responsable en macOS: 100 % en `config.cpp`, `telemetry.cpp`, `telemetry.hpp` e `instrument.hpp`. Esta cifra corresponde a los archivos medidos y **no debe interpretarse como 100 % de todo el módulo**, porque `sample-service` no forma parte de esa medición.
- El nuevo workflow `.github/workflows/observability.yml` configura la telemetría real, ejecuta CTest y aplica un umbral de 90 % a líneas, funciones y regiones (y ramas cuando LLVM las reporte) de los objetos de cobertura seleccionados. El primer resultado del workflow debe revisarse en GitHub; no se afirma que haya pasado hasta verlo ejecutado.
- El 100 % mide que el código se ejecutó, no que esté bien: se necesita complementar la cobertura con aserciones que verifiquen los resultados esperados.

Comandos (desde la raíz del repositorio; macOS):

```sh
cmake -S observability -B observability/build/cov-on \
  -DTECFLIX_ENABLE_TELEMETRY=ON -DTECFLIX_COVERAGE=ON -DCMAKE_BUILD_TYPE=Debug
cmake --build observability/build/cov-on -j8
find observability/build/cov-on -name '*.profraw' -delete
LLVM_PROFILE_FILE="$PWD/observability/build/cov-on/cov-%p.profraw" ctest --test-dir observability/build/cov-on
xcrun llvm-profdata merge -sparse observability/build/cov-on/cov-*.profraw -o observability/build/cov-on/telemetry.profdata
xcrun llvm-cov report ./observability/build/cov-on/telemetry_test -object ./observability/build/cov-on/telemetry_export_test \
  -instr-profile=observability/build/cov-on/telemetry.profdata -ignore-filename-regex='_deps|/tests/|/usr/|Developer'
```

Pruebas rápidas sin OpenTelemetry: `cmake -S observability -B observability/build/off -DTECFLIX_ENABLE_TELEMETRY=OFF` y luego compilar y `ctest`.

## 9. Estado frente a los requisitos

| Requisito (`SPEC.md`) | Estado |
|---|---|
| Métricas con OpenTelemetry en el dashboard | **Hecho** para métricas HTTP (latencia, errores, tráfico) |
| **Traces** | **Pendiente** (no hay SDK de trazas ni pipeline en el Collector) |
| Salud de cada componente | **Parcial**: ahora existe un probe independiente para `sample-service`; faltan targets de las APIs reales y validación en runtime |
| CPU y memoria, throughput de streaming | **Pendiente** |
| Integración con las APIs reales | **Pendiente**: ninguna API usa aún la librería |
| Pruebas unitarias ≥ 90 % | **Medición local reportada** para los archivos de telemetría indicados; el nuevo workflow verifica el umbral en los archivos seleccionados |
| CI en GitHub Actions | **Preparado en código**: existe workflow para tests y cobertura del módulo; pendiente confirmar el resultado de GitHub Actions y decidir CI de Windows |
| Compilar en Windows | **Sin probar** (preset y `vcpkg.json` creados; ejecutar `cmake --preset windows` dentro de `observability/`) |
| Instancias duplicadas (V2.0) | **Parcial**: el dashboard ya filtra por instancia; falta la instancia por defecto y el Collector redundante |

## 10. Plataformas

- **macOS (Apple Silicon):** desarrollo y cobertura; requiere `brew install protobuf`.
- **Linux (GCC 13):** compila y pasa pruebas en modo `OFF`; el modo `ON` requiere instalar Protobuf y curl en el CI (por ejemplo `libprotobuf-dev protobuf-compiler libcurl4-openssl-dev`).
- **Windows (MSVC):** 3 de 4 integrantes lo usan. Ruta prevista: `vcpkg` aporta Protobuf y curl, `FetchContent` sigue trayendo OpenTelemetry v1.20.0 (misma versión en todos). No mezclar un Protobuf instalado en el sistema con el de vcpkg. La primera compilación tarda bastante. Hasta que el equipo lo pruebe, desarrollar con `TECFLIX_ENABLE_TELEMETRY=OFF`.
- La versión de OpenTelemetry está fija en v1.20.0: la API cambia entre versiones (el paquete actual de vcpkg es más nuevo), por eso no se usa el paquete de vcpkg.

## 11. Riesgos y limitaciones conocidas

- Una API sin tráfico y una API caída se ven igual (no hay señal de vida).
- `SERVICE_INSTANCE_ID` por defecto es `instance-1`: dos máquinas sin la variable mezclan sus datos.
- El Collector es único: es un punto único de falla de la observabilidad en la versión 2.0.
- `/health` también se mide; si algo lo consulta con frecuencia aparecerá como una ruta más.
- El histograma llega hasta 1 s: un p99 real mayor se ve como 1 s. Ajustar los buckets si las rutas de streaming son más lentas.
- Contraseña inicial de Grafana (`admin`/`admin`) y puertos sin autenticación: aceptable solo en local.

## 12. Hoja de ruta para terminar el módulo

1. **Validar el nuevo probe de salud**: levantar Blackbox Exporter, `sample-service` y Prometheus; comprobar `probe_success=1`, detener el servicio y comprobar `0`; corregir el nombre `host.docker.internal` si el entorno local lo requiere.
2. **Integrar una API real** cuando exista, siguiendo `observability/docs/integracion.md`; añadir su `/health` como target con `service_name` y `service_instance_id` y validar métricas reales.
3. **Salud y recursos:** implementar métricas de uptime, memoria y CPU del proceso con una interfaz propia, implementaciones por sistema operativo (macOS, Linux, Windows) y pruebas con una versión falsa; añadir paneles de recursos.
4. **Instancia por defecto** = nombre del equipo (hostname), manteniendo IDs únicos entre las dos máquinas.
5. **Traces:** elegir backend con el equipo, agregar el SDK de trazas y un pipeline en el Collector; un span por solicitud en el envoltorio de las APIs.
6. **Métricas de streaming** (bytes entregados, transcodificaciones, duración) cuando exista la API de streaming.
7. **CI:** revisar que `.github/workflows/observability.yml` pase en GitHub, que el umbral de 90 % refleje el alcance acordado, y decidir el trabajo de Windows.
8. **Versión 2.0:** definir conectividad segura a APIs en otra máquina, evaluar un segundo Collector y parametrizar el stack por entorno.
9. **Pruebas de carga** con k6 sobre las APIs, usando este dashboard como evidencia (capturas antes/después de cada optimización) para el paper.
10. **Limpieza:** actualizar o recuperar la documentación README según el estado del repositorio, corregir comentarios y decidir si se elimina el contador redundante.

## 13. Cómo ejecutar el módulo (resumen)

```sh
cd observability/stack && docker compose up -d && cd ../..                     # 1. stack
cmake -S observability -B observability/build                                  # 2. configurar (la primera vez tarda)
cmake --build observability/build --target sample-service                      # 3. compilar
PORT=8090 SERVICE_INSTANCE_ID=maquina-A ./observability/build/sample-service/sample-service   # terminal 1
PORT=8091 SERVICE_INSTANCE_ID=maquina-B ./observability/build/sample-service/sample-service   # terminal 2
seq 1 500 | xargs -P 20 -I{} curl -s -o /dev/null localhost:8090/videos/{}      # 4. tráfico
```

Grafana: `http://localhost:3000/d/tecflix-system/tecflix-system-overview`.

## 14. Registro de decisiones (resumen del historial) (Decisiones en Claude)

- **Prometheus + Grafana** en lugar de Aspire: herramientas conocidas, gratuitas y con Docker Compose. Datos por **OTLP/HTTP** hacia un Collector que desacopla las APIs del almacenamiento.
- **Histograma con buckets propios** en segundos: los predeterminados (0, 5, 10, 25…) daban un p95 de 4.75 s para latencias de 30–115 ms.
- **Módulo independiente** en `observability/` con su propio CMake, por indicación del equipo, sin tocar el código de los demás.
- **Librería detrás de una interfaz** (`HttpMetrics`) con implementación real y muda, para poder probar sin Collector y compilar sin OpenTelemetry.
- **Configuración por variables de entorno** para correr el mismo ejecutable en varias máquinas.
- **Ruta declarada, no URL real**, para controlar la cardinalidad.
- **Prueba con Collector falso** (protocolo JSON) en lugar de comprobar a mano en Prometheus.
- **`TECFLIX_ENABLE_TELEMETRY`** para que quien no trabaja en observabilidad no pague el costo de compilar OpenTelemetry.
- **Dashboard validado con `promtool`** y datos sintéticos antes de entregarlo.
- **SPEC.md compartido** (igual para todos) y **SPEC_DASHBOARD.md** solo con el módulo; el historial detallado anterior quedó en Git.
- **Pruebas con el registrador real, no con la versión muda:** el error `HTTP -1` pasó desapercibido porque `sample-service` se probó con `NoOp`. Pendiente: mover `Instrument` a un encabezado con pruebas unitarias.

## 15. Agent Memory

- **2026-10-13:** Se eligió Prometheus + Grafana para el dashboard. Se configuró OpenTelemetry Collector para recibir métricas OTLP, Prometheus para recopilarlas y Grafana para mostrarlas.
- **2026-10-13:** Se preparó un ejemplo C++ con OpenTelemetry C++ v1.20.0 que simula 40 solicitudes, mide latencia y registra un contador con atributos de método y código HTTP. Se documentaron compilación y consultas en README y se conectó el panel de solicitudes al nombre de métrica del ejemplo.
- **2026-10-13:** El usuario informó conflictos de ramas; al revisar la rama activa no había cambios pendientes y los archivos del ejemplo no existían. Se recreó el ejemplo y el registro Agent Memory en esta rama.
- **Validación de esta recreación:** `git diff --check` y la validación JSON del dashboard pasaron; las consultas del contador y latencia están presentes. Los diagnósticos del editor no encuentran los headers `opentelemetry/...` porque la dependencia no está descargada/configurada. CMake y Docker no están disponibles, así que no se pudo compilar el ejemplo ni ejecutar la pila.
- **2026-10-15:** Se revisó la línea 36 de `examples/cpp-metrics/main.cpp`, que obtiene el meter global con `metrics_api::Provider::GetMeterProvider()`. Si el error indica que `Provider` no está declarado en `opentelemetry::metrics`, la corrección es incluir explícitamente `opentelemetry/metrics/provider.h`; para confirmar que sea ese diagnóstico se necesita el mensaje exacto del compilador.
- **2026-10-15:** Se explicó el estado actual del apartado de observabilidad: Compose levanta Collector, Prometheus y Grafana; el ejemplo C++ simula peticiones y emite contador e histograma; el dashboard provisiona estado de componentes, tasa de solicitudes y p95. Se aclaró que todavía no hay APIs reales instrumentadas, métricas de CPU/memoria ni throughput de streaming, y que la siguiente prioridad es compilar y probar end-to-end antes de integrar las métricas en un servicio HTTP real y completar paneles requeridos por la especificación.
- **2026-10-15:** Se aclaró que la salida inicial de CMake identificando AppleClang y completando los chequeos del compilador es normal y positiva, pero no confirma aún que terminó la configuración, la compilación ni la ejecución; hay que revisar las líneas siguientes hasta el prompt o cualquier error.
- **2026-10-15:** Se explicó cómo inicializar OpenTelemetry para el ejemplo: CMake `FetchContent` descarga automáticamente OpenTelemetry C++ v1.20.0 durante la configuración; primero se levanta el Collector con Docker Compose y luego se configura, compila y ejecuta el demo. Si falla, revisar `build/cpp-metrics/_deps/opentelemetry_cpp-src` y compartir el primer error completo. En el entorno actual `cmake` y `docker` están disponibles.
- **2026-10-15:** Se analizó el fallo de configuración CMake: al activar `WITH_OTLP_HTTP`, OpenTelemetry C++ 1.20.0 requiere Protobuf, ausente en el sistema. El warning de CMP0092 es deprecación no fatal. Recomendación para macOS Apple Silicon/Homebrew: instalar `protobuf`, comprobar `protoc --version` y volver a ejecutar la configuración CMake.
- **2026-10-15:** El usuario confirmó que el ejemplo exporta 40 mediciones correctamente, pero mostró una advertencia de apagado duplicado y un subrayado de IntelliSense para `opentelemetry/metrics/provider.h`. El SDK v1.20.0 incluye ese header bajo `api/include`, presente en los include paths generados; por ello el aviso parece del editor. Se quitó `Shutdown()` explícito porque el destructor de `MeterProvider` apaga su contexto automáticamente; se mantiene `ForceFlush()`. Se habilitó `compile_commands.json` y se documentó cómo conectar el editor a esa base de compilación.
- **2026-10-15:** Se eliminó también el uso innecesario del provider global: `RequestMetrics` ahora recibe el `MeterProvider` directamente, por lo que ya no incluye `opentelemetry/metrics/provider.h` ni depende del singleton. El diagnóstico del editor después del cambio continúa citando ese header eliminado, indicando IntelliSense/base de problemas desactualizada. El código fuente del SDK confirma que el destructor hace el apagado una vez. Se confirmó que la base `compile_commands.json` contiene la ruta correcta `api/include`; se intentó Build_CMakeTools, pero el proyecto raíz no está configurado como CMake.
- **2026-10-15:** Se revisó que Grafana y los tres contenedores están activos, el archivo del dashboard está montado en `/var/lib/grafana/dashboards`, la provisión informa inicio/fin sin error visible y los targets Prometheus responden `up=1`. El mensaje de UID duplicado al importarlo indica que ya existe en Grafana; se documentó abrirlo en la carpeta Tecflix o ir directamente a `/d/tecflix-system/tecflix-system-overview`. También se aclaró que CMP0092 OLD es una advertencia del CMakeLists.txt externo generado dentro de `build/`, no un error del proyecto; no debe editarse esa copia.
- **2026-10-15:** Se investigaron consultas Prometheus vacías. Antes de generar tráfico, los targets estaban `up=1`, pero no había series del demo ni puntos aceptados visibles; se ejecutó el demo localmente y quedaron series `tecflix_http_server_requests_total` y `http_server_request_duration_seconds_{bucket,count,sum}`. Ambas consultas PromQL del usuario devolvieron datos tras la ejecución (tasa aproximada 0.42 solicitudes/s y p95 reportado 4.75 por los límites predeterminados del histograma). Se añadió al README consultar primero el contador bruto y esperar al menos 30 segundos por el scrape interval de 15s antes de `rate()`/percentiles. Pendiente separado: revisar buckets del histograma, ya que los límites por defecto no reflejan bien latencias de 30–115 ms.
- **2026-10-15:** Se interpretaron resultados de Prometheus: el contador acumulado 36 HTTP 200 + 4 HTTP 500 es correcto. `rate(...[1m]) = 0` es posible después de que termine el programa porque no aumentan las muestras recientes. El p95 de 4.75 no es correcto como latencia observada: interpolación sobre buckets predeterminados demasiado amplios (límite 5 s) frente a duraciones de 0.03–0.115 s; ajustar buckets explícitos en segundos y después verificar p95.
- **2026-10-15:** El pipeline local de observabilidad fue comprobado en ejecución: OpenTelemetry C++ envía métricas OTLP, Collector las expone, Prometheus las almacena y las consultas muestran 36 respuestas 200 y 4 respuestas 500. La tasa puede dar 0 una vez termina el tráfico, y el p95 actual no es fiable por los buckets predeterminados. Próxima secuencia recomendada: configurar buckets de latencia adecuados y volver a validarlos; después construir/instrumentar una API HTTP real, incorporar métricas de errores y latencia, luego métricas de streaming/CPU/memoria, dashboard y pruebas de carga.
- **2026-10-16:** Se abordó y validó el paso 1 de observabilidad: se configuraron buckets explícitos para `http.server.request.duration` en segundos (`0.025, 0.050, 0.075, 0.100, 0.125, 0.150, 0.200, 0.300, 0.500, 1.000`) mediante una View del SDK OpenTelemetry C++ v1.20.0 asociada al nombre/unidad/instrumentación correctos. CMake compiló el ejecutable. Docker Desktop se inició, los tres contenedores quedaron activos y se ejecutó el demo: Prometheus mostró 36 respuestas 200, 4 respuestas 500, buckets acumulados 8<=50ms, 20<=75ms, 32<=100ms y 40<=125ms; p95 consultado devolvió ~0.11915 segundos. README explica la configuración y la interpretación aproximada. Para repetir, debe estar iniciada la pila (`docker compose up -d`), recompilar el demo y ejecutarlo, esperar un scrape. La advertencia CMP0092 de dependencia CMake no bloquea.
- **2026-10-16:** El usuario compartió capturas de Grafana después de ajustar buckets: el panel p95 se mantiene alrededor de 118.5–120 ms, coherente con el simulador (~30–115 ms y p95 estimado por buckets); el panel de tasa recibe series y el contador conserva 36 respuestas 200 + 4 respuestas 500. Una caída/reinicio visible en valores acumulados puede ser reset del proceso, comportamiento esperado para un Counter; `rate()` maneja resets. Paso de validación de buckets satisfactorio. Siguiente fase recomendada: dejar el simulador como smoke test y, antes de escalar a tres APIs, elegir un servicio HTTP real mínimo e instrumentar un endpoint `/health` y una ruta de prueba con contador, status y histograma; validar consultas/dashboard y explicar el código paso a paso. El repositorio aún no contiene APIs de negocio implementadas.
- **2026-10-19:** Se realizó revisión técnica del repositorio. Estado: única capacidad funcional implementada es observabilidad C++ (librería `tecflix_telemetry`, simulador `sample-service`, Collector/Prometheus/Grafana y pruebas); no hay frontend, servicios REST de usuarios/metadata/streaming, persistencia, carga/transcodificación de video, auth, CI ni pruebas de carga. Build actual exitoso; CTest 9/9 y Compose config/JSON/YAML válidos. `README.md` está desactualizado y refiere `examples/cpp-metrics`/`docker compose` raíz que no corresponden a rutas actuales bajo `observability/`. Hallazgo de compatibilidad: métrica usa atributo `http.response.status.code` en vez del convencional `http.response.status_code`; revisar nomenclatura y dashboard. Configuración OTLP solo resuelve nombre/instancia/endpoint base, con instance id estático por defecto; faltan validación, opciones endpoint/protocol/headers/errores y pruebas de fallos. `HttpMetrics::Record` acepta status/duración arbitrarios y depende del servicio para evitar rutas de cardinalidad alta. Stack local no dispone de readiness/healthchecks, retención/operación remota ni HA. Dashboard editado localmente quedó sin modificar por la revisión; queries esperan las etiquetas actuales y tasa de errores debería controlar denominador cero. Próximos pasos: corregir docs y naming, construir API HTTP mínima real con contrato/error handling, instrumentarla y probar end-to-end; luego auth/usuarios/almacenamiento, metadata/Mongo, pipeline streaming FFmpeg/cuotas/visibilidad, frontend, seguridad/operación/CI y HA/carga V2.
- **2026-10-19:** El usuario confirmó que revisó los tres cambios locales y los guardó en commit `954c960` (`Mejora en Grafana`); la rama `Dashboard_Sistema` está limpia. La rama contiene 11 commits más que `main` local, mientras que `main` local y `origin/main` divergen (main local 2 commits adelante y 1 atrás); frente a `origin/main`, la rama está 1 commit atrás y 13 adelante. Recomendación: no hacer merge directo; primero `git fetch origin`, revisar `git log origin/main..HEAD` y `git diff origin/main...HEAD` (incluido el renombre de SPEC.md a SPEC_DASHBOARD.md), probar desde build limpio (`cmake -S observability -B observability/build`, build y CTest), luego publicar rama y abrir PR a `main`; verificar que el PR no incluya cambios inesperados y que el equipo acepte el renombre de la especificación. Build actual no está disponible en el worktree recién cambiado, así que debe configurarse de nuevo antes de confirmar las pruebas de este commit.
- **2026-10-27:** El responsable validó en macOS la compilación del módulo con telemetría habilitada, 19/19 pruebas y 100 % de cobertura reportada en `config.cpp`, `telemetry.cpp`, `telemetry.hpp` e `instrument.hpp`. La validación end-to-end con `sample-service` mostró los paneles de tasa, latencia por ruta, errores 5xx, códigos HTTP y promedio/p95/p99. Los valores informados (8,90 % de errores globales y latencia p95 cercana a 197 ms) son resultados de una ejecución del simulador, no umbrales esperados para las APIs reales.
- **2026-10-27:** Se preparó comprobación HTTP de salud independiente mediante Blackbox Exporter, con un target inicial para `sample-service / local-dev`; se añadió el panel `Salud de servicios (/health)`, se amplió la fuente de variables para incluir series `probe_success`, se creó `observability/docs/integracion.md` y un workflow de CI que ejecuta tests y comprueba cobertura. Pendiente: validar Compose, Prometheus y el panel nuevo en runtime, y observar el resultado real del workflow en GitHub.