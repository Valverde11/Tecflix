# GUIA_LOADTEST.md — Guía de pruebas de carga

Esta guía describe la implementación C++ de pruebas de carga en `loadtest/`, su uso y la interpretación de resultados. La especificación de requisitos y decisiones está en `SPEC_LOADTEST.md`.

## 1. Estado actual

**Implementado:** CLI C++17 `tecflix-loadtest`, escenarios `login` y `read-only`, perfiles `smoke`, `fixed` y `capacity`, medición por ruta y reportes JSON/CSV. El runner compara dos conjuntos de URLs proporcionadas como `single` y `dual`. Hay seis tests CTest para configuración, métricas, runner, comparación, reportes y argumentos del CLI. El workflow raíz incluye esos tests dentro del job de Streaming. El CLI restringe `smoke` a hosts locales/privados incluso si un host externo aparece en la allowlist; `fixed` y `capacity` conservan la allowlist para entornos autorizados.

**Implementado en Fase 4:** Compose/Dockerfiles y Nginx locales para single y una topología dual parcial. Single ejecuta una instancia de cada API; dual ejecuta dos Metadata con Mongo compartido, pero mantiene una instancia de Users y Streaming. La comparación y sus archivos JSON/CSV llevan la marca **“provisional, solo valida la herramienta”**; no equivale al requisito dual de dos instancias por API.

**Pendiente:** resolver con el equipo la persistencia/coordinación de Users y Streaming antes de afirmar topología dual completa; validar localmente la compilación y los tests del cambio reciente de restricción por perfil; job/reporte de cobertura específicos de loadtest; campañas reales. Los límites locales son hasta 200 VUs para `read-only` y 50 para `login`; `capacity` completa el par `single`/`dual` y para por los umbrales configurados. No se han registrado resultados reales de despliegues V1/V2. La cobertura local supera 90%, pero aún no se aplica en CI.

El PDF V2 pide encontrar límites mediante carga/estrés concurrente y comparar uno contra dos servidores: observar cuándo se afecta el rendimiento y cuándo se rechazan solicitudes. El harness es C++ nativo (no k6); el dashboard puede aportar evidencia complementaria, pero no es requisito para compilar o ejecutar el generador.

## 2. Qué componentes se medirán y por qué

| Componente | Responsabilidad actual | Motivo para incluirlo en pruebas |
|---|---|---|
| API de Usuarios | `single` y `dual` URL requeridas para ambos escenarios | En `read-only`: preparación de login, perfil y cuota. En `login`: recibe las autenticaciones medidas. |
| API de Metadata | URLs `single` y `dual`, requeridas para `read-only` | Consulta catálogo paginado y detalle del video de prueba. |
| API de Streaming | URLs `single` y `dual`, requeridas para `read-only` | Consulta estado, manifiesto HLS y un segmento de un video ya listo. |
| Topología dual | La proporciona el equipo por medio de las URLs `dual` (por ejemplo, balanceador hacia dos instancias) | El runner no crea balanceadores, despliegues, replicación de datos ni storage. |
| Métricas del runner | Agregadas dentro del proceso C++ | Mide cantidad, códigos HTTP, fallos y latencia por ruta; resume throughput, p50, p95 y respuestas 429/503. No recolecta CPU/RAM del servidor ni consulta Prometheus automáticamente. |

Las topologías, cuentas de prueba y video deben estar preparados por el equipo antes de correr el programa. Las URLs deben apuntar a orígenes HTTP sin rutas ni credenciales.

## 3. Cómo se interpretará una ejecución

**Implementado:** `smoke` ejecuta una etapa con 1 usuario virtual; `fixed` mantiene el máximo configurado; `capacity` recorre niveles crecientes (1, 5, 10, 20, 40, 80, 120, 160, 200), recortados al máximo configurado y sin duplicados. Los topes son 200 VUs en `read-only` y 50 en `login`; cada etapa dura entre 1 y 300 segundos. El valor predeterminado de duración es 10 s (30 s en `capacity`). No son metas de desempeño.

El runner ejecuta las dos configuraciones de forma secuencial con igual carga y alterna cuál inicia primero entre etapas. En `capacity`, completa el par `single`/`dual` del nivel y se detiene si cualquiera supera estrictamente `--max-failure-percent` o `--max-p95-ms`. Defaults: 1% de fallos y 2000 ms p95. El porcentaje acepta 0–100 inclusive; p95 debe ser finito y no negativo. `comparison.json` indica si se detuvo, índice de etapa, VUs, límites y criterios por topología. `--cooldown-seconds` acepta 0–300 s; el predeterminado es 15 s para `capacity` y 0 s para otros perfiles.

Los fallos incluyen errores de red, respuestas no 2xx y respuestas exitosas con contenido que no satisface la validación del escenario. `rejected_requests` cuenta específicamente respuestas HTTP 429 y 503; el primer fallo observado puede ser otro estado o un error de red. El p50/p95 se estima con histogramas exponenciales por buckets y no es una medición de precisión arbitraria.

## 4. Seguridad y repetibilidad

- Ejecutar únicamente contra servicios propios/locales o un entorno de prueba autorizado. Los hosts privados o locales se aceptan por defecto; un hostname externo debe figurar exactamente en `LOADTEST_ALLOWED_HOSTS`.
- **Restricción implementada en CLI; validación local pendiente:** `smoke` solo acepta hosts locales/privados y rechaza hosts externos aunque estén en `LOADTEST_ALLOWED_HOSTS`. `fixed` y `capacity` sí admiten hosts externos incluidos exactamente en esa allowlist, con autorización explícita del entorno.
- No se permite HTTPS en esta versión del parser de destino; las URL con credenciales, rutas, queries o fragmentos se rechazan.
- `read-only` no modifica registros; requiere credenciales de una cuenta existente y video existente listo en ambas topologías. El escenario `login` sí genera operaciones de autenticación.
- No guardar credenciales, tokens ni reportes voluminosos en Git. La ruta recomendada para resultados es `load-tests/results/`.
- Usar límites del generador conservadores y ejecutar pruebas únicamente cuando el destino y el volumen de carga estén verificados.

## 5. Construcción y ejecución

El proyecto de carga se construye independientemente de las APIs:

```bash
cmake -S loadtest -B build/loadtest -DBUILD_TESTING=ON
cmake --build build/loadtest --parallel
ctest --test-dir build/loadtest --output-on-failure
```

Requiere CMake 3.20+, compilador C++17, Threads, nlohmann-json 3.11+ y cpp-httplib que ya está versionado en el repositorio. Para ver todas las opciones:

```bash
build/loadtest/tecflix-loadtest --help
```

El CLI actualmente anuncia esta forma de invocación:

```text
tecflix-loadtest --single-users-url URL --dual-users-url URL --output DIR
  [--single-metadata-url URL --single-streaming-url URL]
  [--dual-metadata-url URL --dual-streaming-url URL]
  [--profile smoke|fixed|capacity] [--scenario read-only|login]
  [--max-vus N] [--stage-seconds N] [--cooldown-seconds N]
  [--max-failure-percent N] [--max-p95-ms N]
```

Los umbrales se aplican al perfil `capacity`; se pueden pasar también en otros perfiles, donde no detienen el escalamiento. Las credenciales se pasan solo como variables de entorno. Ejemplo `read-only` con perfil `smoke` contra destinos locales (reemplazar direcciones y datos por servicios de prueba preparados):

```bash
export LOADTEST_USERNAME='cuenta-de-prueba'
export LOADTEST_PASSWORD='contraseña-de-prueba'
export LOADTEST_VIDEO_ID='0123456789abcdef0123456789abcdef'

build/loadtest/tecflix-loadtest \
  --single-users-url http://127.0.0.1:8081 \
  --single-metadata-url http://127.0.0.1:8082 \
  --single-streaming-url http://127.0.0.1:8083 \
  --dual-users-url http://127.0.0.1:8181 \
  --dual-metadata-url http://127.0.0.1:8182 \
  --dual-streaming-url http://127.0.0.1:8183 \
  --scenario read-only --profile smoke --max-vus 1 --stage-seconds 10 \
  --output load-tests/results
```

El escenario `login` solo requiere las URLs de Usuarios `single` y `dual`; no se le pasan metadata, streaming ni `LOADTEST_VIDEO_ID`:

```bash
build/loadtest/tecflix-loadtest \
  --single-users-url http://127.0.0.1:8081 \
  --dual-users-url http://127.0.0.1:8181 \
  --scenario login --profile smoke --stage-seconds 10 \
  --output load-tests/results
```

Ajustar deliberadamente `--profile`, `--max-vus`, `--stage-seconds`, `--cooldown-seconds`, `--max-failure-percent` y `--max-p95-ms` a la capacidad segura del entorno. Las corridas `smoke` son solo locales/privadas; el parser rechaza hosts externos incluso si están en la allowlist. Para `fixed` y `capacity`, los destinos externos requieren `LOADTEST_ALLOWED_HOSTS` con el hostname exacto y autorización previa.

Cada ejecución crea un subdirectorio `run-<UTC timestamp>` dentro de `--output` con `comparison.json` y `comparison.csv`. JSON contiene configuración, métricas por etapa/ruta, diferencias entre topologías y primer nivel con fallos. CSV permite comparar solicitudes, errores, rechazos, throughput, p50/p95 y cambios porcentuales.

La suite CTest incluye `TecflixLoadtestConfig`, `TecflixLoadtestMetrics`, `TecflixLoadtestRunner`, `TecflixLoadtestComparison`, `TecflixLoadtestReport` y `TecflixLoadtestCli`: cubre configuración/URLs/perfiles, histogramas y fallos, escenarios HTTP simulados, comparación, reportes JSON/CSV y argumentos. Se añadieron casos focalizados para validaciones del CLI/config, errores de configuración del runner y validación/continuación de niveles de comparación. El workflow raíz compila el proyecto y ejecuta CTest dentro del job de Streaming; todavía no hay job específico ni aplicación de cobertura para loadtest. Las pruebas verifican el harness, no son una campaña de desempeño sobre APIs desplegadas.

**Validación local del 2026-11-28 (Arch Linux, GCC 16.2.1, CMake 4.5.1):** build completo del CMake raíz correcto; CTest completo pasó 10/10. Con `--coverage`/gcov, excluyendo cabeceras de STL y nlohmann-json, se midieron 752/813 líneas ejecutadas (92,50%) en los seis `.cpp` de `loadtest/src/`. Por archivo: `cli.cpp` 91,19%, `comparison.cpp` 96,36%, `config.cpp` 98,63%, `metrics.cpp` 98,25%, `report.cpp` 92,86% y `runner.cpp` 86,84%. El agregado local supera el 90%; CI aún no mide ni aplica ese umbral.

La validación anterior corresponde al estado previo de la restricción por perfil. La implementación actual y sus casos CLI están pendientes de compilación y CTest por el responsable del entorno; no se atribuyen resultados a ese cambio hasta recibir esa salida.

## 6. Topologías locales provisionales

La definición, los riesgos de estado y las alternativas para dual completa están en [OPCIONES_HA_USERS_STREAMING.md](OPCIONES_HA_USERS_STREAMING.md). **No modificar ni borrar datos mientras single y dual estén levantados:** comparten los volúmenes de Users y Streaming sin coordinación entre procesos. En esta comparación solo se permiten `login` y `read-only` contra una cuenta y un video listo, sembrados antes de iniciar la topología dual. No subir, borrar, transcodificar ni cambiar cuotas/metadata.

Puertos publicados solo en loopback:

| Topología | Users | Metadata | Streaming | Mongo |
|---|---:|---:|---:|---|
| Single | 8081 | 8082 | 8083 | solo red Docker |
| Dual provisional | 8181 | 8182 | 8183 | el mismo contenedor iniciado por Compose single |

Las comprobaciones de Compose consultan `/health` activamente. Nginx usa healthchecks pasivos (`max_fails` y `fail_timeout`); no incluye el módulo comercial de health checks activos. Metadata apunta al mismo MongoDB y en dual escala por `METADATA_REPLICAS` (2 por defecto). Users y Streaming quedan con una sola instancia en ambas configuraciones.

Mantén la misma terminal para que los secretos estén disponibles a los dos proyectos. No guardes estos valores en archivos versionados:

```bash
export AUTH_SECRET="$(openssl rand -hex 32)"
export SERVICE_SECRET="$(openssl rand -hex 32)"
```

Desde la raíz, valida configuración y levanta primero single. Usa el flujo autorizado del equipo para crear una cuenta de prueba y un video listo; el mismo Mongo y los volúmenes nombrados se reutilizan al iniciar dual.

```bash
docker compose -p tecflix-loadtest-single \
  -f deploy/loadtest/docker-compose.single.yml config -q
docker compose -p tecflix-loadtest-single \
  -f deploy/loadtest/docker-compose.single.yml up -d --build
docker compose -p tecflix-loadtest-single \
  -f deploy/loadtest/docker-compose.single.yml ps

docker compose -p tecflix-loadtest-dual \
  -f deploy/loadtest/docker-compose.dual.yml config -q
METADATA_REPLICAS=2 docker compose -p tecflix-loadtest-dual \
  -f deploy/loadtest/docker-compose.dual.yml up -d --build
docker compose -p tecflix-loadtest-dual \
  -f deploy/loadtest/docker-compose.dual.yml ps

for port in 8081 8082 8083 8181 8182 8183; do
  curl --fail "http://127.0.0.1:${port}/health"
done
```

Ambas topologías usan los mismos Dockerfiles, variables de entorno, volúmenes nombrados y Mongo. La única variación intencional de cantidad de procesos es Metadata: 1 en single y 2 por defecto en dual. Nginx publica single en 8081–8083 y dual en 8181–8183 para que el ejecutable pueda ejecutar ambas configuraciones pareadas.

Ejemplo `smoke` read-only contra las dos entradas locales (sustituye los datos de la cuenta/video por los del fixture ya preparado):

```bash
export LOADTEST_USERNAME='cuenta-de-prueba'
export LOADTEST_PASSWORD='contraseña-de-prueba'
export LOADTEST_VIDEO_ID='0123456789abcdef0123456789abcdef'

build/loadtest/tecflix-loadtest \
  --single-users-url http://127.0.0.1:8081 \
  --single-metadata-url http://127.0.0.1:8082 \
  --single-streaming-url http://127.0.0.1:8083 \
  --dual-users-url http://127.0.0.1:8181 \
  --dual-metadata-url http://127.0.0.1:8182 \
  --dual-streaming-url http://127.0.0.1:8183 \
  --scenario read-only --profile smoke --stage-seconds 10 \
  --output load-tests/results
```

Este resultado y cualquier JSON/CSV generado por el harness se etiqueta **“provisional, solo valida la herramienta”**. No lo presentes como evidencia de rendimiento de una arquitectura V2 dual completa. Detén dual antes de single. `down` conserva los datos; `down -v` elimina fixtures y Mongo:

```bash
docker compose -p tecflix-loadtest-dual \
  -f deploy/loadtest/docker-compose.dual.yml down
docker compose -p tecflix-loadtest-single \
  -f deploy/loadtest/docker-compose.single.yml down
```

## 7. Mantenimiento con nuevos prompts

Cuando un prompt agregue una decisión o cambie el alcance:

1. Actualizar **Agent memory** y los requisitos/criterios de `SPEC_LOADTEST.md`.
2. Actualizar esta guía con el comportamiento real, la razón de diseño y la forma validada de usarlo.
3. Si aún no hay código, dejarlo indicado como propuesta o pendiente, nunca como funcionalidad existente.
4. Al modificar código, verificar la ruta funcional implicada y actualizar los comandos y resultados que la documentación presenta.
