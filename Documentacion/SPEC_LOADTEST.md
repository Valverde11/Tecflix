# SPEC_LOADTEST.md — Especificación de pruebas de carga

Este documento define el alcance y las decisiones para las pruebas de carga de Tecflix. Complementa `SPEC.md` y `SPEC_DASHBOARD.md`; si hay una contradicción, prevalece `SPEC.md`. Es la fuente de verdad para cualquier implementación de load testing que se agregue en prompts posteriores.

**Estado:** implementación C++ disponible en `loadtest/`; permite ejecutar escenarios y comparar resultados, pero aún no documenta mediciones reales de un despliegue V2.
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
4. Limitar la concurrencia a 50 usuarios virtuales para `read-only`, 10 para `login`, y la duración de cada etapa a 1–300 segundos. Estos límites protegen al generador/entorno; no son objetivos de capacidad del sistema.
5. Admitir solo destinos HTTP sin ruta, query ni credenciales integradas. Por defecto aceptar hosts locales/privados; un destino externo requiere hostname exacto en `LOADTEST_ALLOWED_HOSTS` y autorización explícita.
6. Leer `LOADTEST_USERNAME` y `LOADTEST_PASSWORD` del entorno; `read-only` también requiere `LOADTEST_VIDEO_ID` de un video ya listo, con ID hexadecimal minúsculo de 32 caracteres.
7. El escenario `read-only` debe medir las rutas funcionales usadas por el recorrido y nunca modificar usuarios, metadata o videos. No usar únicamente `/health`.
8. Comparar los mismos escenarios y niveles de carga para `single` y `dual`; alternar cuál topología se ejecuta primero entre niveles y respetar los tiempos de enfriamiento configurados.
9. Contabilizar como fallos respuestas no 2xx, errores de red y respuestas HTTP cuyo contenido no cumpla la validación del escenario. Identificar 429 y 503 por separado como respuestas de rechazo observadas.
10. En perfil `capacity`, terminar de ejecutar el par de topologías del primer nivel con fallos y no subir a niveles posteriores. No presentar esto como límite absoluto: es el primer fallo observado dentro del perfil aplicado.
11. No asumir objetivos numéricos de latencia, throughput ni criterios de aprobación hasta que el equipo los acuerde. No presentar resultados de pruebas del harness como mediciones de despliegue.
12. Mantener credenciales, secretos y resultados voluminosos fuera del control de versiones.

## 4. Criterios de aceptación

- El ejecutable C++ se compila y sus pruebas CMake/CTest cubren configuración, métricas, escenarios HTTP y comparación.
- Se puede ejecutar cada escenario contra destinos autorizados con credenciales desde el entorno.
- El reporte JSON y CSV distingue perfil, escenario, destinos, etapas, concurrencia, duración, latencia, throughput, fallos, respuestas 429/503 y cambios entre topologías.
- La comparación mantiene el mismo escenario y carga para las dos configuraciones; en `capacity` completa ambas antes de detenerse al detectar fallos.
- La documentación explica limitaciones y no atribuye causalidad a métricas que solo sean correlativas.
- Ningún umbral de aprobación se considera acordado hasta quedar escrito en esta sección y en la memoria del agente.

## 5. Decisiones y límites vigentes

- **Herramienta:** C++17; ejecutable independiente `tecflix-loadtest`. El uso de k6 que menciona `SPEC_DASHBOARD.md` no aplica a esta implementación.
- **Rutas:** `read-only` ejecuta login de preparación, perfil, cuota, catálogo, detalle, estado de streaming, manifiesto HLS y un segmento. `login` repite `POST /api/auth/login` durante cada etapa.
- **Topologías:** el ejecutable recibe URLs de Usuarios, Metadata y Streaming para `single` y `dual`; balanceador, réplicas, datos compartidos y storage deben estar desplegados previamente.
- **Límites:** `smoke` usa 1 usuario; `fixed` sostiene el máximo; `capacity` recorre niveles crecientes. Máximo 50 VUs read-only, 10 login y 300 s por etapa. En `capacity`, por defecto 30 s por etapa y 15 s de cooldown.
- **Resultados:** JSON y CSV se escriben en un subdirectorio de ejecución bajo la ruta `--output`; se recomienda `load-tests/results/`, ya ignorada por Git. Latencias p50/p95 son estimaciones de un histograma exponencial.
- **Ejecución:** manual en ambiente local/de pruebas autorizado. No hay generación de carga en CI ni mediciones reales V1/V2 registradas todavía. Los umbrales de desempeño están pendientes.

Las comparaciones producidas por el harness y sus servidores falsos verifican el código del generador, no sustituyen una medición de infraestructura. El primer error observado depende del entorno, datos, duración, rutas y perfil de carga; no representa un límite universal.

## Agent instructions

1. Antes de trabajar en load testing, leer esta especificación, su sección **Agent memory**, `SPEC.md` y las instrucciones relevantes de `SPEC_DASHBOARD.md`.
2. Tratar este archivo como la fuente de verdad del alcance de load testing. Si el código o un prompt entra en conflicto con una decisión documentada, señalar la discrepancia y actualizar la especificación de forma coherente.
3. No inventar endpoints, cargas, umbrales, credenciales, topologías ni resultados. Si faltan detalles no bloqueantes, dejar explícita la limitación; si la decisión cambia significativamente el comportamiento, pedir confirmación.
4. Mantener `Agent instructions` estable. Registrar en `Agent memory` las decisiones importantes en una línea breve, con motivo y estado cuando corresponda.
5. Cada vez que un prompt agregue o cambie requisitos, actualizar primero esta especificación y luego mantener sincronizada `GUIA_LOADTEST.md` con lo implementado y lo que siga pendiente.
6. Documentar la diferencia entre lo especificado, lo implementado y lo validado. No presentar una recomendación o plan como funcionalidad existente.
7. Para cambios de código, seguir las convenciones del repositorio, añadir pruebas enfocadas cuando corresponda y ejecutar la validación mínima relevante.

## Agent memory

- El generador reside en `loadtest/` y todo su código de producción y pruebas es C++17; CMake lo construye aparte de las APIs.
- Se usan escenarios read-only (una autenticación de preparación y recorrido funcional con token) y login repetido (mide autenticaciones); se evita modificar datos de usuarios y videos.
- Los perfiles están limitados a 50 VUs para read-only y 10 para login, con etapas de 1–300 s; son límites de seguridad del harness, no objetivos de rendimiento.
- Las dos topologías se ejecutan secuencialmente con igual concurrencia; el orden inicial alterna por nivel. `capacity` conserva el par y detiene niveles posteriores ante fallos.
- Los destinos externos requieren aprobación del operador y un hostname exacto en `LOADTEST_ALLOWED_HOSTS`; se rechazan URLs HTTPS, rutas, queries y credenciales embebidas.
- Se escriben `comparison.json` y `comparison.csv` con resultados observados; 429/503 se marcan como rechazos, pero todos los no-2xx, errores de red y respuestas inválidas cuentan como fallos.
- El build y pruebas con servicios simulados validan el harness; no equivalen a una ejecución de carga contra despliegues V1/V2 reales. No se han acordado umbrales ni resultados oficiales.
