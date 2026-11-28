# SPEC_LOADTEST.md — Especificación de pruebas de carga

Este documento define el alcance y las decisiones para las pruebas de carga de Tecflix. Complementa `SPEC.md` y `SPEC_DASHBOARD.md`; si hay una contradicción, prevalece `SPEC.md`. Es la fuente de verdad para cualquier implementación de load testing que se agregue en prompts posteriores.

**Estado:** existe el generador C++17 `tecflix-loadtest`, con pruebas de configuración, métricas, runner, comparación, reportes y CLI. Los topes 200/50, niveles capacity, umbrales configurables y reporte JSON de parada están implementados y probados. La validación por perfil de destinos está implementada: smoke solo acepta hosts locales/privados; fixed y capacity conservan la allowlist. La topología parcial de Fase 4 (Metadata x2; Users y Streaming x1) fue validada por el equipo, pero sigue marcada provisional y no equivale al requisito dual. La Fase 5 agrega un job CI dedicado de compilación/CTest y cobertura LLVM ≥90%; la primera ejecución remota sigue pendiente y CI no ejecuta carga real. La Fase 6 documenta el método de campañas locales y el registro de evidencia; no se han ejecutado campañas ni hay resultados reales de despliegue V2. La topología dual completa está **bloqueada: requiere decisión de diseño del equipo** por estado local no coordinado en Users y Streaming.
**Alcance:** carga y estrés concurrente de APIs Tecflix y comparación de topologías de una instancia y dos instancias. No cambia la lógica de negocio.

## 1. Objetivo

Medir el comportamiento de Tecflix bajo usuarios concurrentes y obtener evidencia reproducible para:

1. Identificar capacidad, latencia, tasa de errores y posibles cuellos de botella.
2. Comparar un despliegue de una instancia con otro de dos instancias detrás de un balanceador.
3. Facilitar la correlación manual de resultados con el dashboard de `observability/` y las optimizaciones que se implementen. El ejecutable no consulta Prometheus ni recoge métricas de CPU/RAM.

La sección 4.3 de `SPEC.md` y el requisito V2 del PDF del proyecto requieren determinar los límites con usuarios concurrentes y comparar uno y dos servidores. Se implementa un ejecutable nativo C++17, integrado con CMake, en lugar de depender de k6, Python, JavaScript o scripts auxiliares.

## 2. Alcance y límites

### Incluido

- Escenarios concurrentes `read-only` y `login`, con perfiles `smoke`, `fixed` y `capacity`.
- Comparación pareada de dos configuraciones de servicios, denominadas `single` y `dual`, usando la misma concurrencia y duración por etapa.
- Métricas por ruta de cantidad de solicitudes, códigos HTTP, fallos y percentiles de latencia, además de resumen de throughput, p50, p95 y respuestas 429/503.
- Reportes de comparación en JSON y CSV, incluyendo configuración de destinos, métricas por etapa/ruta, diferencias entre topologías y fallos observados.
- Documentación de condiciones, resultados y limitaciones para que las comparaciones sean interpretables.

### Fuera de alcance inicial

- Cambiar la lógica de las APIs o el sistema de observabilidad para mejorar artificialmente los resultados.
- Probar una instancia o servicio de terceros sin autorización.
- Borrar o alterar datos compartidos del equipo durante una ejecución.
- Fijar objetivos numéricos de latencia, throughput, concurrencia o disponibilidad sin acuerdo explícito.

## 3. Requisitos

1. Implementar el generador y sus pruebas en C++17 bajo `loadtest/`; compilar con CMake y usar las dependencias disponibles en el proyecto (cpp-httplib vendorizado, nlohmann-json y Threads).
2. Admitir los escenarios `read-only` (recorrido funcional de lectura por Usuarios, Metadata y Streaming) y `login` (autenticaciones repetidas contra Usuarios).
3. Admitir `smoke` (1 usuario virtual), `fixed` (máximo configurado) y `capacity` (niveles crecientes, limitados al máximo configurado).
4. Limitar la concurrencia a 200 usuarios virtuales para `read-only` y 50 para `login`, con etapas de 1–300 segundos. `--max-vus` permite escoger un máximo menor dentro de esos topes. Implementado; estos límites protegen al generador/entorno y no son objetivos de capacidad del sistema.
5. Admitir solo destinos HTTP sin ruta, query ni credenciales integradas. El perfil `smoke` solo puede apuntar a hosts locales o privados y debe rechazar hosts externos incluso si aparecen en `LOADTEST_ALLOWED_HOSTS`, para evitar tráfico remoto accidental. En los perfiles `fixed` y `capacity`, un host externo requiere hostname exacto en `LOADTEST_ALLOWED_HOSTS` y autorización explícita.
6. Leer `LOADTEST_USERNAME` y `LOADTEST_PASSWORD` del entorno; `read-only` también requiere `LOADTEST_VIDEO_ID` de un video ya listo, con ID hexadecimal minúsculo de 32 caracteres.
7. El escenario `read-only` debe medir las rutas funcionales usadas por el recorrido y nunca modificar usuarios, metadata o videos. No usar únicamente `/health`.
8. Comparar los mismos escenarios y niveles de carga para `single` y `dual`; alternar cuál topología se ejecuta primero entre niveles y respetar los tiempos de enfriamiento configurados.
9. Contabilizar como fallos respuestas no 2xx, errores de red y respuestas HTTP cuyo contenido no cumpla la validación del escenario. Identificar 429 y 503 por separado como respuestas de rechazo observadas.
10. El criterio acordado para `capacity` es degradar una etapa si el porcentaje de fallos es estrictamente mayor que el máximo configurado o p95 es estrictamente mayor que el máximo configurado, en cualquiera de las topologías. Los defaults son 1% y 2000 ms; `--max-failure-percent` y `--max-p95-ms` los reemplazan. El porcentaje se valida entre 0 y 100 inclusive; p95 debe ser finito y no negativo. Se completa el par single/dual del nivel antes de parar; el JSON identifica nivel, VUs, límites, topología, métrica y valor que causaron la parada.
11. Mantener fuera de alcance la subida/transcodificación: consume cuota, genera datos persistentes y mezcla el costo de transcodificar con el de servir segmentos. Es una limitación explícita de esta entrega.
12. Compilar y probar el generador en un job CI dedicado, medir cobertura LLVM del componente y aplicar mínimo de 90%; nunca ejecutar carga real en CI. El job publica el reporte JSON de cobertura como artefacto.
13. Las corridas `smoke` son exclusivamente locales/privadas; nunca ejecutarlas contra hosts externos.
14. Mantener credenciales, secretos, videos, quarantine y resultados voluminosos fuera del control de versiones.
15. La topología parcial con Metadata x2 y Users/Streaming x1 se etiqueta en Compose, README, guía y cada reporte JSON/CSV como **“provisional, solo valida la herramienta”**. No cuenta como cumplimiento de dos instancias por API ni produce evidencia de capacidad/HA comparable con el requisito dual.
16. Documentar un procedimiento manual, local y repetible para `smoke`, `fixed` y `capacity` en comparación `single`/`dual`; registrar parámetros, commit, máquina, ventana UTC, artefactos crudos y capturas pertinentes de Grafana. No ejecutar campañas reales en CI ni redactar el paper con IA. Las mediciones de la topología parcial conservan la etiqueta provisional y no son evidencia de HA dual.

## 4. Criterios de aceptación

- El ejecutable C++ se compila y sus pruebas CMake/CTest cubren configuración, argumentos del CLI, métricas, escenarios HTTP, reportes y comparación; CI publica/verifica cobertura del componente.
- Se puede ejecutar cada escenario contra destinos autorizados con credenciales desde el entorno.
- El reporte JSON y CSV distingue perfil, escenario, destinos, etapas, concurrencia, duración, latencia, throughput, fallos, respuestas 429/503 y cambios entre topologías; incluye la etiqueta `provisional, solo valida la herramienta` para la topología parcial de esta fase.
- La comparación mantiene el mismo escenario y carga para las dos configuraciones; en `capacity` completa ambas antes de detenerse al detectar fallos.
- Para `capacity`, los límites por defecto son estrictamente >1% de fallos o >2000 ms de p95 en cualquiera de las topologías; `--max-failure-percent` y `--max-p95-ms` validan y reemplazan esos límites.
- El componente obtiene al menos 90% de cobertura en CI; la carga de servicios reales no se ejecuta en pipelines. La cobertura local gcov previamente reportada supera 90%; el nuevo job LLVM aún requiere su primera ejecución remota.
- La documentación explica limitaciones y no atribuye causalidad a métricas que solo sean correlativas.
- El perfil `capacity` usa umbrales explícitos y configurables de fallos/p95, e identifica en el reporte la primera etapa degradada y el criterio que la degradó.
- La guía contiene comandos repetibles por perfil/escenario para la comparación local, indica metadatos y capturas a conservar, declara límites de Prometheus/Grafana y no afirma resultados no ejecutados.
- Las pruebas de carga reales son manuales y locales; CI solo compila y ejecuta pruebas automatizadas con cobertura, sin generar tráfico hacia servicios.

## 5. Decisiones acordadas y estado de implementación

| # | Decisión acordada | Motivo | Estado |
|---|---|---|---|
| 1 | C++ `tecflix-loadtest`; k6 descartado para esta entrega. | Mantener la pila de los servicios, evitar dependencias externas y producir reportes JSON propios. | Implementado: CLI C++ y pruebas de componente existentes. |
| 2 | Nginx en Docker; compose separado para single y dual. En dual, round-robin con healthchecks pasivos. | Comparar despliegues repetibles de una y dos réplicas por servicio. | Fase 4 validada por el equipo en topología parcial; dual completo bloqueado por decisión 9. |
| 3 | Balancear Users, Metadata y Streaming; excluir la aplicación web. | El requisito V2 pide redundancia en APIs, no en frontend. | Parcial provisional: Nginx balanceará Metadata x2; Users y Streaming quedarán x1. Metadata puede compartir Mongo; las tres APIs no estarán duplicadas. |
| 4 | Tope read-only 200 VUs, login 50; etapas máximo 300 s; `--max-vus` configurable dentro del límite. Capacity sube por niveles hasta el máximo (p. ej. 1, 5, 10, 20, 40, 80, 120, 160, 200). | Explorar concurrencia de manera progresiva sin exceder el límite acordado. | Implementado: topes y niveles progresivos; `--max-vus` puede reducir el máximo. |
| 5 | Capacity se degrada si fallos >1% o p95 >2000 ms en cualquiera de las topologías. Añadir `--max-failure-percent` y `--max-p95-ms`, y reportar etapa/criterio de parada. | No detener por errores aislados; definir un punto de degradación reproducible y ajustable. | Implementado: defaults, overrides validados, par completo y criterio/etapa en JSON. |
| 6 | Mantener login y read-only; excluir subida/transcodificación de esta entrega. | La subida consume cuota, genera datos persistentes y mezcla costo de transcodificación con entrega de segmentos. | Implementado como alcance del runner; documentada como limitación. |
| 7 | CI compila loadtest y corre sus tests con cobertura LLVM mínima de 90%; cargas reales solo locales, no en CI. | Verificar el componente y publicar evidencia de cobertura sin generar carga contra servicios en pipelines. | Implementado: job raíz dedicado, cobertura LLVM, verificación al 90% y artefacto JSON; primera ejecución remota pendiente. |
| 8 | Smoke solo puede usar destinos locales/privados, aunque un host externo aparezca en la allowlist; `fixed` y `capacity` conservan la allowlist para entornos autorizados. | Evitar que una corrida de humo toque un host externo por error. | Implementado en CLI y cubierto por tests enfocados; compilación y CTest de este cambio pendientes de validación local. |
| 9 | No duplicar Users ni Streaming hasta que el equipo decida cómo coordinar su estado y persistencia; preservar el requisito de Users de archivo binario con árbol B en disco. | Evitar inconsistencias entre procesos y no alterar silenciosamente el diseño de persistencia acordado para V1. | **Bloqueado: requiere decisión de diseño del equipo.** La topología parcial de Fase 4 no equivale a dos instancias por API. |
| 10 | Documentar método y datos crudos reproducibles para campañas locales; no redactar el paper ni presentar la topología parcial como HA. | Permitir que el equipo produzca evidencia verificable sin inventar resultados ni confundir correlación con causalidad. | Procedimiento y plantilla de registro documentados en Fase 6; campañas reales pendientes del equipo. |

### Escenarios y rutas implementados

- **Read-only:** obtiene token con `POST /api/auth/login` durante preparación (fuera de la etapa medida); por iteración consulta `GET /api/users/me`, `GET /api/users/:id/quota`, `GET /api/metadata/videos?page=1&page_size=20&mine=true`, `GET /api/metadata/videos/:id`, `GET /api/videos/:id`, `GET /api/videos/:id/index.m3u8` y un `GET /api/videos/:id/segments/:segment` descubierto en el manifiesto. El manifiesto debe incluir un segmento con formato `segment_#####.ts`; el estado de streaming debe ser `ready`. Las demás rutas se contabilizan como fallo ante respuesta no 2xx, sin validar el contenido de su respuesta.
- **Login:** repite `POST /api/auth/login` durante la etapa y valida que la respuesta sea HTTP 200 y contenga un token string no vacío.
- No se prueba `/health` como carga funcional, ni se suben/borran videos.

### Estado validado frente a objetivo

- **Especificado:** las diez decisiones de la tabla y los criterios anteriores.
- **Implementado:** CLI C++17; escenarios `login`/`read-only`; perfiles `smoke`/`fixed`/`capacity`; topes 200/50 VUs y niveles capacity `1, 5, 10, 20, 40, 80, 120, 160, 200` recortados al máximo; etapas 1–300 s; cooldown 0–300 s; umbrales estrictos configurables; comparación pareada; JSON `capacity_stop`; métricas y reportes JSON/CSV.
- **Fase 4 validada por el equipo, parcial:** Compose single (Users/Metadata/Streaming x1) y dual provisional (Metadata x2, Users/Streaming x1), Mongo compartido, Nginx y Dockerfiles. Toda comparación generada por esta herramienta incluye `provisional, solo valida la herramienta` en JSON y CSV mientras Users/Streaming no estén duplicados.
- **Fase 5 implementada:** job CI dedicado, opción `TECFLIX_COVERAGE` en `loadtest/CMakeLists.txt`, validación con `observability/scripts/check_coverage.py` y artefacto JSON. La sintaxis YAML pasó PyYAML local; la primera ejecución remota y el umbral alcanzado siguen pendientes. Las campañas de carga no se ejecutan en CI.
- **Fase 6:** procedimiento, comandos reproducibles, registro de parámetros y capturas Grafana documentados; no se ejecutaron campañas ni se generaron resultados.
- **Pendiente/bloqueado:** primera ejecución remota del job y confirmación de cobertura ≥90%; dual completa con Users x2 y Streaming x2 (**decisión 9: bloqueado, requiere decisión de diseño del equipo**); campañas reales a cargo del equipo.
- **Validado:** el 2026-11-28, con GCC 16.2.1 y CMake 4.5.1, el build completo del CMake raíz pasó y CTest pasó 10/10. gcov midió 752/813 líneas (92.50%) en `loadtest/src/*.cpp`: CLI 91.19%, comparison 96.36%, config 98.63%, metrics 98.25%, report 92.86% y runner 86.84%. Se probaron límites estrictos, ambos criterios, ambas topologías, validación de argumentos y campos del JSON de parada. El equipo confirmó Fase 4 como validada y aceptó su condición provisional. La sintaxis del workflow de Fase 5 pasó PyYAML; falta primera ejecución remota. No se afirman resultados de desempeño real.

Las mediciones del harness no sustituyen una campaña real sobre ambas topologías. El primer error observado depende del entorno, datos, duración, rutas y perfil, y no representa un límite universal.

## Agent instructions

1. Antes de trabajar en load testing, leer esta especificación, su sección **Agent memory**, `SPEC.md` y las instrucciones relevantes de `SPEC_DASHBOARD.md`.
2. Tratar este archivo como la fuente de verdad del alcance de load testing. Si el código o un prompt entra en conflicto con una decisión documentada, señalar la discrepancia y actualizar la especificación de forma coherente.
3. No inventar endpoints, cargas, umbrales, credenciales, topologías ni resultados. Si faltan detalles no bloqueantes, dejar explícita la limitación; si la decisión cambia significativamente el comportamiento, pedir confirmación.
4. Mantener `Agent instructions` estable. Registrar en `Agent memory` las decisiones importantes en una línea breve, con motivo y estado cuando corresponda.
5. Cada vez que un prompt agregue o cambie requisitos, actualizar primero esta especificación y luego mantener sincronizada `GUIA_LOADTEST.md` con lo implementado y lo que siga pendiente.
6. Documentar la diferencia entre lo especificado, lo implementado y lo validado. No presentar una recomendación o plan como funcionalidad existente.
7. Para cambios de código, seguir las convenciones del repositorio, añadir pruebas enfocadas cuando corresponda y ejecutar la validación mínima relevante.

## Agent memory

- Decisión 1: se eligió el generador C++ `tecflix-loadtest` y se descartó k6 por mantener la pila C++, evitar dependencias externas y generar JSON propio. **Estado:** ejecutable, tests de componente y job CI/cobertura LLVM específicos implementados; primera ejecución remota pendiente.
- Decisión 2: se acordó Nginx en Docker, Compose single/dual y round-robin con healthchecks pasivos. **Estado:** topología parcial validada por el equipo; dual completa bloqueada por falta de estado coordinado en Users/Streaming.
- Decisión 3: se acordó balancear Users, Metadata y Streaming; la web queda fuera porque el requisito V2 se refiere a APIs. **Estado:** topología parcial provisional Metadata x2, Users/Streaming x1; no equivale al requisito dual.
- Decisión 4: se acordó máximo 200 VUs read-only, 50 login y etapas de hasta 300 s para explorar mayor concurrencia. **Estado:** implementado y validado con tests de topes y niveles capacity.
- Decisión 5: se acordó degradación de capacity ante >1% de fallos o p95 >2000 ms, configurable por argumentos CLI. **Estado:** implementado y validado; el par termina antes de parar y JSON registra etapa, topología y criterio.
- Decisión 6: se excluyó subida/transcodificación porque consume cuota, crea datos persistentes y mezcla costos distintos. **Estado:** runner solo implementa login/read-only.
- Decisión 7: se acordó compilar y probar loadtest en CI con cobertura LLVM mínima de 90%, sin generar carga real. **Estado:** job dedicado, opción CMake, verificación con `observability/scripts/check_coverage.py` y artefacto JSON implementados; primera ejecución remota pendiente.
- Decisión 8: smoke rechaza destinos externos incluso si están en `LOADTEST_ALLOWED_HOSTS`, para impedir tráfico remoto involuntario; fixed y capacity mantienen la allowlist para entornos autorizados. **Estado:** implementado en CLI con pruebas enfocadas; compilación y CTest de este cambio pendientes de validación local.
- Decisión 9: no duplicar Users ni Streaming hasta acordar cómo coordinar estado/persistencia, y preservar el archivo binario con árbol B en disco de Users. **Estado:** bloqueado: requiere decisión de diseño del equipo; la topología parcial Metadata x2, Users/Streaming x1 está implementada y no equivale al requisito dual.
- Decisión 10: entregar método reproducible y datos crudos para el paper, sin redactar su texto ni inventar resultados; limitar campañas a entornos locales autorizados y marcar como provisional toda comparación de la topología parcial. **Estado:** procedimiento documentado en Fase 6; corridas reales pendientes del equipo.
- Fase 5: usar cobertura LLVM en un job CI dedicado, aplicar mínimo de 90% con `observability/scripts/check_coverage.py`, subir JSON como artefacto y no ejecutar carga real en pipelines. **Estado:** implementado; sintaxis YAML validada localmente, primera ejecución remota pendiente.
- C++17, rutas, perfiles actuales, formatos de reporte y sus diferencias con las decisiones aprobadas se detallan en las secciones 3 y 5. Todavía no hay resultados reales de campañas V2 ni métricas de cobertura que declarar.
- Fase 2: probar reportes, degradación de comparación y validación del CLI mediante casos aislados; extraer el parser con el cambio mínimo para inyectar argumentos/entorno y no depender de secretos ni servicios externos. **Estado:** cerrada en `810e62b`; después de Fase 3, los tests de umbral del contrato real también pasan.
- Fase 3: subir topes y reemplazar la parada ante cualquier fallo por umbrales configurables. **Estado:** implementada y validada el 2026-11-28; build completo, CTest 10/10 y gcov core 92.50%.
- Fase 4: el equipo validó la topología parcial y aceptó su condición provisional; no se cambió estado ni persistencia de Users/Streaming. La dual completa sigue bloqueada por la decisión 9. Evidencia: Users usa `user_repository.hpp:33`, `user_service.hpp:100`, `binary_file.cpp:10` y `quota_operation_journal.cpp:108`; Streaming mantiene `VideoService.h:79,86`, recupera registros en `VideoService.cpp:69` y escribe segmentos en `FfmpegTranscoder.cpp:27`. Metadata sí puede compartir Mongo (`repository_factory.cpp:14`). Compose, guía y reportes JSON/CSV de la topología Metadata x2 se etiquetan “provisional, solo valida la herramienta”.
- Fase 6: documentar el protocolo y los datos requeridos para campañas locales reproducibles, sin ejecutar cargas ni redactar el paper. **Estado:** procedimiento documentado; resultados crudos pendientes de ejecución por el equipo.
