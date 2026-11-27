# SPEC_LOADTEST.md — Especificación de pruebas de carga

Este documento define el alcance y las decisiones para las pruebas de carga de Tecflix. Complementa `SPEC.md` y `SPEC_DASHBOARD.md`; si hay una contradicción, prevalece `SPEC.md`. Es la fuente de verdad para cualquier implementación de load testing que se agregue en prompts posteriores.

**Estado:** existe el generador C++ inicial en `loadtest/`. Faltan cambios acordados de límites/umbral, topologías Docker, casos de prueba de reporte/CLI y CI; no hay resultados reales de despliegue V2 registrados.
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
- Reportes de comparación en JSON y CSV, incluyendo configuración de destinos y primer nivel con fallos observados.
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
4. Limitar la concurrencia objetivo a 200 usuarios virtuales para `read-only`, 50 para `login`, y la duración de cada etapa a 1–300 segundos. `--max-vus` permite escoger un valor menor. Estos límites protegen al generador/entorno; no son objetivos de capacidad del sistema.
5. Admitir solo destinos HTTP sin ruta, query ni credenciales integradas. Los perfiles `smoke` solo pueden apuntar a hosts locales o privados; nunca a hosts externos. En otros perfiles, un host externo requiere hostname exacto en `LOADTEST_ALLOWED_HOSTS` y autorización explícita.
6. Leer `LOADTEST_USERNAME` y `LOADTEST_PASSWORD` del entorno; `read-only` también requiere `LOADTEST_VIDEO_ID` de un video ya listo, con ID hexadecimal minúsculo de 32 caracteres.
7. El escenario `read-only` debe medir las rutas funcionales usadas por el recorrido y nunca modificar usuarios, metadata o videos. No usar únicamente `/health`.
8. Comparar los mismos escenarios y niveles de carga para `single` y `dual`; alternar cuál topología se ejecuta primero entre niveles y respetar los tiempos de enfriamiento configurados.
9. Contabilizar como fallos respuestas no 2xx, errores de red y respuestas HTTP cuyo contenido no cumpla la validación del escenario. Identificar 429 y 503 por separado como respuestas de rechazo observadas.
10. En perfil `capacity`, considerar degradada una etapa si `100 * fallos / solicitudes > 1` o p95 `> 2000 ms` en cualquiera de las topologías. Ambos umbrales son configurables por `--max-failure-percent` y `--max-p95-ms`; completar el par single/dual del nivel y detener el escalamiento, registrando etapa y criterio.
11. Mantener fuera de alcance la subida/transcodificación: consume cuota, genera datos persistentes y mezcla el costo de transcodificar con el de servir segmentos. Es una limitación explícita de esta entrega.
12. Compilar y probar el generador en CI con cobertura; nunca ejecutar carga real en CI.
13. Las corridas `smoke` son exclusivamente locales/privadas; nunca ejecutarlas contra hosts externos.
14. Mantener credenciales, secretos, videos, quarantine y resultados voluminosos fuera del control de versiones.

## 4. Criterios de aceptación

- El ejecutable C++ se compila y sus pruebas CMake/CTest cubren configuración, argumentos del CLI, métricas, escenarios HTTP, reportes y comparación; CI publica/verifica cobertura del componente.
- Se puede ejecutar cada escenario contra destinos autorizados con credenciales desde el entorno.
- El reporte JSON y CSV distingue perfil, escenario, destinos, etapas, concurrencia, duración, latencia, throughput, fallos, respuestas 429/503 y cambios entre topologías.
- La comparación mantiene el mismo escenario y carga para las dos configuraciones; en `capacity` completa ambas antes de detenerse al detectar fallos.
- Para `capacity`, los límites por defecto son estrictamente >1% de fallos o >2000 ms de p95 en cualquiera de las topologías; `--max-failure-percent` y `--max-p95-ms` validan y reemplazan esos límites.
- El componente obtiene al menos 90% de cobertura en CI; la carga de servicios reales no se ejecuta en pipelines.
- La documentación explica limitaciones y no atribuye causalidad a métricas que solo sean correlativas.
- El perfil `capacity` usa umbrales explícitos y configurables de fallos/p95, e identifica en el reporte la primera etapa degradada y el criterio que la degradó.
- Las pruebas de carga reales son manuales y locales; CI solo compila y ejecuta pruebas automatizadas.

## 5. Decisiones acordadas y estado de implementación

| # | Decisión acordada | Motivo | Estado |
|---|---|---|---|
| 1 | C++ `tecflix-loadtest`; k6 descartado para esta entrega. | Mantener la pila de los servicios, evitar dependencias externas y producir reportes JSON propios. | Parcial: CLI C++ existe; faltan las pruebas y CI acordadas. |
| 2 | Nginx en Docker; compose separado para single y dual. En dual, round-robin con healthchecks pasivos. | Comparar despliegues repetibles de una y dos réplicas por servicio. | Pendiente: faltan Compose y configuración. |
| 3 | Balancear Users, Metadata y Streaming; excluir la aplicación web. | El requisito V2 pide redundancia en APIs, no en frontend. | Pendiente: despliegue de las topologías. |
| 4 | Tope read-only 200 VUs, login 50; etapas máximo 300 s; `--max-vus` configurable dentro del límite. Capacity sube por niveles hasta el máximo (p. ej. 1, 5, 10, 20, 40, 80, 120, 160, 200). | Explorar concurrencia de manera progresiva sin exceder el límite acordado. | Pendiente: código actual conserva topes 50/10 y niveles antiguos. |
| 5 | Capacity se degrada si fallos >1% o p95 >2000 ms en cualquiera de las topologías. Añadir `--max-failure-percent` y `--max-p95-ms`, y reportar etapa/criterio de parada. | No detener por errores aislados; definir un punto de degradación reproducible y ajustable. | Pendiente: hoy se detiene por cualquier fallo y no acepta esos argumentos. |
| 6 | Mantener login y read-only; excluir subida/transcodificación de esta entrega. | La subida consume cuota, genera datos persistentes y mezcla costo de transcodificación con entrega de segmentos. | Implementado como alcance del runner; documentada como limitación. |
| 7 | CI compila loadtest y corre tests con cobertura; cargas reales solo locales, no en CI. | Verificar código sin generar carga contra servicios en pipelines. | Pendiente: falta job de CI y reporte/umbral de cobertura. |
| 8 | Smoke solo puede usar destinos locales/privados, nunca hosts externos. | Evitar generar carga involuntaria contra servicios remotos. | Pendiente: el parser actual admite host externo incluido en allowlist sin distinguir perfil. |

### Escenarios y rutas implementados

- **Read-only:** obtiene token con `POST /api/auth/login` durante preparación; por iteración consulta `GET /api/users/me`, `GET /api/users/:id/quota`, `GET /api/metadata/videos?page=1&page_size=20&mine=true`, `GET /api/metadata/videos/:id`, `GET /api/videos/:id`, `GET /api/videos/:id/index.m3u8` y un `GET /api/videos/:id/segments/:segment` descubierto en el manifiesto.
- **Login:** repite `POST /api/auth/login` durante la etapa y valida que la respuesta contenga un token.
- No se prueba `/health` como carga funcional, ni se suben/borran videos.

### Estado validado frente a objetivo

- **Especificado:** las ocho decisiones de la tabla y los criterios anteriores.
- **Implementado hoy:** CLI C++17, escenarios login/read-only, perfiles smoke/fixed/capacity con topes actuales 50/10, métricas/reportes JSON/CSV y comparación single/dual contra URLs suministradas.
- **Pendiente de implementar:** decisiones 2, 3 (despliegue), 4, 5, 7 y la restricción de perfil de la decisión 8; también tests de reportes y argumentos del CLI.
- **Validado:** hasta el estado existente del repositorio; no afirmar cobertura de 90%, topologías funcionales ni campañas de desempeño real sin sus respectivas ejecuciones y evidencias.

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

- Decisión 1: se eligió el generador C++ `tecflix-loadtest` y se descartó k6 por mantener la pila C++, evitar dependencias externas y generar JSON propio. **Estado:** ejecutable implementado; falta CI/cobertura.
- Decisión 2: se acordó Nginx en Docker, Compose single/dual y round-robin con healthchecks pasivos. **Estado:** pendiente de implementar.
- Decisión 3: se acordó balancear Users, Metadata y Streaming; la web queda fuera porque el requisito V2 se refiere a APIs. **Estado:** topologías pendientes.
- Decisión 4: se acordó máximo 200 VUs read-only, 50 login y etapas de hasta 300 s para explorar mayor concurrencia. **Estado:** parser y perfiles actuales aún limitan 50/10.
- Decisión 5: se acordó degradación de capacity ante >1% de fallos o p95 >2000 ms, configurable por argumentos CLI. **Estado:** falta implementar; hoy para ante cualquier fallo.
- Decisión 6: se excluyó subida/transcodificación porque consume cuota, crea datos persistentes y mezcla costos distintos. **Estado:** runner solo implementa login/read-only.
- Decisión 7: se acordó compilar y probar en CI con cobertura, sin generar carga real en CI. **Estado:** job pendiente.
- Decisión 8: smoke se restringe a destinos locales/privados para impedir tráfico remoto involuntario. **Estado:** falta hacer que el parser aplique esta regla; hoy acepta hosts externos autorizados en cualquier perfil.
- C++17, rutas, perfiles actuales, formatos de reporte y sus diferencias con las decisiones aprobadas se detallan en las secciones 3 y 5. Todavía no hay resultados reales de campañas V2 ni métricas de cobertura que declarar.
