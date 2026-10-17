# Especificación Técnica del Proyecto: Tecflix (Spec-Driven Development)

## 1. Visión General
Tecflix es un sistema distribuido de streaming de video diseñado para la plataforma web, compuesto por un frontend reactivo, múltiples servicios backend en C++ organizados en microservicios/APIs, almacenamiento estructurado y binario, observabilidad completa y mecanismos de alta disponibilidad[cite: 1, 2, 3].

---

## 2. Stack Tecnológico General
- **Frontend Web:** ReactJS 18+ con TypeScript.
- **Reproducción de Video:** Reproductor open-source (Shaka Player o Dash.js) compatible con protocolos MPEG-DASH / HLS[cite: 2].
- **Backend APIs:** C++ (C++17/C++20) para API de Usuarios, API de Metadata y API de Streaming[cite: 2].
- **Base de Datos y Almacenamiento:** 
  - Archivos binarios estructurados en disco con **Árbol B** indexado para el API de Usuarios[cite: 2].
  - **MongoDB** para el API de Metadata[cite: 2].
  - Sistema de archivos local / Red distribuida para bloques transcodificados de video[cite: 2].
- **Procesamiento de Video:** FFMPEG (transcodificación de formatos y fragmentación)[cite: 2].
- **Observabilidad:** OpenTelemetry (Traces y Métricas) integrado con Prometheus / Grafana / Aspire[cite: 3].
- **Pruebas y CI/CD:** GitHub Actions (Pipelines), pruebas unitarias C++ (GoogleTest / Catch2) y React (Vitest / Jest), Pruebas Funcionales E2E con Playwright[cite: 3].
- **Pruebas de Carga:** k6 / Locust / Apache JMeter[cite: 3].

---

## 3. Arquitectura del Sistema y Componentes

### 3.1. Aplicación Web (ReactJS + TypeScript)
- **Ruta/Ubicación:** `/frontend`
- **Requerimientos Funcionales:**
  - **Autenticación y Registro:** Formularios de Login y Registro integrados con el API de Usuarios[cite: 2].
  - **Catálogo y Paginación:** Visualización de videos propios y públicos ordenables por título, autor y fecha de carga[cite: 2]. La ordenación y paginación se realiza delegando la consulta al API de Metadata[cite: 2].
  - **Gestión de Videos:** Subida de videos, modificación de metadata, eliminación y alternancia de visibilidad (Público/Privado)[cite: 1, 2].
  - **Cuotas de Almacenamiento:** Control estricto del límite asignado por usuario. Bloquear la subida cuando la cuota sea superada[cite: 2].
  - **Reproductor:** Integración de Shaka Player / Dash.js para streaming adaptativo[cite: 2].

### 3.2. API de Usuarios (C++)
- **Ruta/Ubicación:** `/services/user-service`
- **Interfaz:** REST API en C++ (usando Crow, Pistache o HTTPLib)[cite: 2].
- **Requerimientos Funcionales:**
  - Gestión CRUD completa de usuarios (Registrar, Consultar, Actualizar, Eliminar)[cite: 2].
  - Persistence Engine: Guardado directo en **archivo binario** personalizado[cite: 2].
  - Indexación: Implementación nativa de un **Árbol B en disco** para optimización de búsquedas[cite: 2].

### 3.3. API de Metadata (C++)
- **Ruta/Ubicación:** `/services/metadata-service`
- **Interfaz:** REST API en C++[cite: 2].
- **Requerimientos Funcionales:**
  - Gestión de metadatos de videos: autor, duración, fecha de carga, título, tema, etiquetas y estado de visibilidad[cite: 1, 2].
  - Integración directa con MongoDB usando `mongocxx` driver[cite: 2].
  - Endpoints para listado con soporte de ordenación (título, autor, fecha) y paginación[cite: 2].

### 3.4. API de Streaming (C++)
- **Ruta/Ubicación:** `/services/streaming-service`
- **Interfaz:** REST API en C++[cite: 2].
- **Requerimientos Funcionales:**
  - Transcodificación de videos en background al momento de la carga mediante **FFMPEG** (conversión a segmentos HLS/DASH)[cite: 2].
  - Servicio de entrega de bloques/chunks de video hacia el reproductor web de manera eficiente[cite: 2].

### 3.5. Dashboard y Observabilidad
- **Instrumentación:** OpenTelemetry C++ SDK en las 3 APIs y OpenTelemetry JS en el Frontend[cite: 3].
- **Métricas:** Latencia de endpoints, tasa de errores (HTTP 5xx/4xx), uso de CPU/memoria y throughput de streaming[cite: 3].
- **Visualización:** Configuración de Prometheus + Grafana o Aspire para consumo e interpretación de métricas y traces[cite: 3].

---

## 4. Requerimientos de Arquitectura V2.0 (Alta Disponibilidad y Calidad)

### 4.1. Eliminación de Single Point of Failure (SPOF)
- Mapeo y ejecución en al menos 2 instancias independientes/separadas para cada API (Usuarios, Metadata, Streaming)[cite: 3].
- Implementación de balanceador de carga (Nginx / HAProxy / Traefik) frente a los servicios[cite: 3].

### 4.2. Optimización de Performance
- Identificación y resolución de al menos 5 cuellos de botella basados en las métricas de la V1.0 (ej. caching de metadata, hilos de FFMPEG, optimización de B-Tree)[cite: 3].

### 4.3. Pruebas de Carga y Estrés
- Scripts de carga simulando usuarios concurrentes para medir capacidad límite de 1 servidor vs. 2 servidores con balanceador[cite: 3].

### 4.4. CI/CD y Pruebas
- **GitHub Workflows:** Pipeline de compilación y empaquetado automático de ejecutables C++ y cliente React[cite: 3].
- **Cobertura:** Mínimo **90% de cobertura de código** mediante pruebas unitarias en todos los componentes[cite: 3].
- **Pruebas Funcionales:** Pruebas E2E automatizadas usando **Playwright** totalmente integradas en GitHub Actions[cite: 3].

---

## 5. Criterios de Aceptación para Agentes de IA
1. Todo código C++ debe estar estructurado bajo principios del paradigma orientado a objetos (OOP) y buenas prácticas de memoria[cite: 1, 5].
2. Los componentes del frontend deben estar fuertemente tipados con Interfaces de TypeScript sin usar `any`.
3. Todos los endpoints REST deben cumplir con respuestas en formato JSON estandarizado y códigos de estado HTTP apropiados.
4. El pipeline en GitHub Actions debe ejecutarse sin errores antes de consolidar ramas o crear tags para V1.0 y V2.0[cite: 3, 4].

## 6. Agent Memory

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