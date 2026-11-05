# Agent instructions
1. Act as an expert in software engineering, modern C++ (C++17 or later), ReactJS with TypeScript, distributed systems and video streaming.
2. Use the model "[model name]".
3. Design and build "Tecflix", a video streaming system, using object-oriented programming in every component. Project for CE 2103 (Algorithms and Data Structures II, TEC, II Semester 2026).
4. Work with spec-driven development: this file is the source of truth. If something is unclear or contradictory, do not silently make it up: record your assumption in "Agent memory".
5. Use the SPEC.md file as memory to store the agent's decisions. For this, use the section called "Agent memory".
6. Before starting any task, read "Agent memory". Every time you make an important decision, add it there in one short line (what you decided and why). Do not modify the "Agent instructions" section.


# Especificación del proyecto

Este archivo es **idéntico para todos los integrantes** del equipo. Describe qué debe hacer Tecflix y cómo debe comportarse. Los detalles de un componente concreto viven en su propio archivo (por ejemplo `SPEC_DASHBOARD.md` para el dashboard del sistema); si hay un conflicto, **manda este archivo**. Un cambio a este archivo se acuerda con todo el equipo y se publica por pull request; nadie lo modifica por su cuenta.

## 1. Contexto y objetivos

Proyecto del curso Algoritmos y Estructuras de Datos II (CE 2103), Escuela de Ingeniería en Computadores, TEC, II Semestre 2026. Vale el 20 % de la nota del curso y se realiza en grupos de 4 personas.

- **Objetivo general:** diseñar e implementar un sistema de streaming de video.
- **Objetivos específicos:** implementar componentes básicos de un sistema de streaming; investigar cómo funcionan estos sistemas; implementar una solución que cumpla la arquitectura propuesta; usar el paradigma orientado a objetos en diseño e implementación.
- Los requerimientos del enunciado son intencionalmente poco detallados: se espera investigar, ser creativo, aplicar principios de ingeniería y **justificar cada decisión**.

## 2. Reglas del proyecto (afectan a todo el código)

- Git y GitHub son obligatorios. Las versiones 1.0 y 2.0 se entregan como tags o ramas aparte.
- **Integración:** si los componentes no funcionan en conjunto ("todo sirve por aparte, pero nada junto"), la nota total del proyecto es cero.
- **Defensa:** si cualquier integrante no puede explicar qué hace un componente o una decisión de diseño, la nota del proyecto es cero. Todo código generado por un agente debe ser entendible por el equipo y justificarse en la revisión.
- **Malas prácticas:** cada mala práctica de programación detectada penaliza 5 puntos (ver sección 7).
- **Uso de IA:** se permite libremente para el código. **El paper no puede redactarse con IA** (cero puntos si se detecta): los agentes nunca escriben texto del paper; solo pueden aportar datos medidos (gráficas, tablas, mediciones).
- La fecha de entrega es la que indique el TEC Digital.

## 3. Componentes de la versión 1.0

```
 Navegador ── Aplicación web (React + TS) ──┬──► API de usuarios   (C++, archivo binario + árbol B)
                                            ├──► API de metadata   (C++, MongoDB)
                                            └──► API de streaming  (C++, FFmpeg)
 Todas las APIs ──(OpenTelemetry)──► Dashboard del sistema (métricas y traces)
```

### 3.1 Aplicación web
- ReactJS con TypeScript. Al abrir la página se muestra un formulario para **registrarse o autenticarse**; el registro usa la API de usuarios.
- Autenticado, el usuario ve una lista de videos: **los propios más los públicos de otros**, ordenable por título, autor (quien sube) y fecha de carga. **El ordenamiento lo hace la API de metadata** y la lista **se pagina** (no se trae completa).
- Puede subir y eliminar videos, modificar su metadata, gestionar el acceso (público o privado) y reproducirlos.
- La reproducción usa un reproductor open source (Shaka Player o dash.js), por lo que el servidor de streaming debe ser compatible con él.
- Cada usuario tiene una **cuota de almacenamiento fija** (no modificable). Al alcanzarla, no se permite subir más videos.

### 3.2 API de usuarios
- REST escrito en C++, se ejecuta en el servidor. Operaciones: registrar, actualizar, consultar (y eliminar; ver supuesto en "Agent memory").
- Los usuarios se guardan en un **archivo en formato binario**. Para acelerar las búsquedas se usa un **árbol B en disco**.

### 3.3 API de metadata
- REST escrito en C++. Gestiona autor, duración, fecha de carga, tema, título, etiquetas, entre otros (incluye la visibilidad pública/privada).
- Los datos se guardan en **MongoDB**. Ordena y pagina la lista de videos.

### 3.4 API de streaming
- Escrita en C++. Recibe las peticiones del reproductor y devuelve los bloques de video en el formato que este espera.
- **Transcodifica** el video cuando el usuario lo sube, usando **FFmpeg**.

### 3.5 Dashboard del sistema
- Todos los componentes usan **OpenTelemetry** para generar **traces y métricas**, que se consumen en una aplicación como Aspire, Prometheus o similar.
- Debe permitir a un administrador consultar **errores, salud de cada componente, desempeño de cada componente y estadísticas generales**. Las métricas concretas son criterio del equipo, pero deben bastar para entender el rendimiento del sistema.
- Especificación detallada: `SPEC_DASHBOARD.md`.

## 4. Contrato de telemetría (obligatorio para toda API en C++)

Para que el dashboard sea uniforme y el mismo ejecutable corra en dos máquinas sin recompilar, cada API cumple lo siguiente (detalles y ejemplos en `SPEC_DASHBOARD.md`):

1. Usa la librería `tecflix_telemetry` (carpeta `observability/telemetry`): crea un objeto `Telemetry` al arrancar con `ConfigFromEnv("<nombre-del-servicio>")` y llama a `ForceFlush()` al apagarse.
2. Nombres de servicio fijos: `user-service`, `metadata-service`, `streaming-service`.
3. Configuración **solo por variables de entorno** (nunca valores fijos en el código): `OTEL_SERVICE_NAME`, `SERVICE_INSTANCE_ID` (distingue la máquina A de la B), `OTEL_EXPORTER_OTLP_ENDPOINT` (por defecto `http://localhost:4318`).
4. Expone `GET /health` que responde `200` con `{"status":"ok"}`.
5. Cada ruta se mide con su **patrón declarado** (`/users/:id`, `/videos/:id`), **nunca con la URL real**, para evitar series infinitas en Prometheus.
6. La compilación acepta la opción `TECFLIX_ENABLE_TELEMETRY` (por defecto `ON`). Con `OFF` se usa una versión muda de la librería para compilar rápido y probar sin Collector; el CI y la entrega usan `ON`.

## 5. Requerimientos de la versión 2.0

- **Eliminar puntos únicos de falla (SPOF).** Salvo la aplicación web, todos los componentes se ejecutan en **dos máquinas separadas**. Esto implica retos (balanceo, estado compartido, consistencia, qué pasa cuando una instancia cae) que deben investigarse, resolverse y justificarse.
- **Mejorar el rendimiento.** Con el dashboard de la versión 1.0, encontrar **al menos 5 oportunidades de optimización** y aplicarlas, midiendo el antes y el después.
- **Pruebas de carga y estrés.** Determinar los límites del sistema con un servidor contra dos, con usuarios concurrentes: en qué punto se degrada el rendimiento y cuándo se rechazan peticiones. Se documenta cómo se encontraron esos límites.
- **Integración continua.** Todos los componentes se compilan y empaquetan con GitHub pipelines. Todos tienen pruebas unitarias con **cobertura mínima de 90 %**. En la revisión, los ejecutables se descargan desde GitHub y se ejecutan en las máquinas requeridas.
- **Pruebas funcionales** con Playwright o similar, integradas en el pipeline de CI.

## 6. Entregables y rúbrica

**Código (80 %).** Enlace al repositorio de GitHub. Se usa desarrollo guiado por especificación: este `SPEC.md` es la entrada de los agentes de IA. Reparto de los 100 puntos de código:

| Aspecto | Valor |
|---|---|
| Aplicación web | 10 |
| API de usuarios | 15 |
| API de metadata | 5 |
| API de streaming | 15 |
| Dashboard del sistema | 10 |
| Mejoras para eliminar puntos únicos de falla | 20 |
| Pruebas de carga | 15 |
| Integración continua | 5 |
| Pruebas funcionales | 5 |
| **Total** | **100** |

**Paper (20 %).** Se redacta **sin ayuda de IA** y trata la versión 2.0. Debe explicar: (1) qué cambios mejoraron el rendimiento, cómo se midió antes y después, con comparativas; (2) las decisiones de diseño para eliminar puntos únicos de falla; (3) cómo se diseñaron y ejecutaron las pruebas de carga y cuáles son los límites del sistema. Se califica ortografía y formato estándar.

## 7. Calidad de código (convenciones del equipo, a confirmar)

Cada mala práctica detectada cuesta 5 puntos, así que estas reglas aplican a todos los componentes:

- Orientación a objetos en cada componente; C++17; gestión de memoria con RAII y punteros inteligentes (sin `new`/`delete` manuales).
- Sin valores fijos de configuración en el código (puertos, URLs, rutas, nombres de máquina): se leen del entorno o de un archivo de configuración. Sin secretos en el repositorio.
- Sin `using namespace` en archivos de encabezado; encabezados con `#pragma once`.
- Errores manejados explícitamente; respuestas REST en JSON con códigos HTTP apropiados.
- TypeScript estricto, sin `any`.
- Funciones cortas, sin código duplicado, comentarios que explican el **porqué**.
- Cada componente trae sus pruebas unitarias.

## 8. Estructura del repositorio y entorno (propuesta, a confirmar)

```
/
├── SPEC.md                  # este archivo (igual para todos)
├── SPEC_DASHBOARD.md        # dashboard del sistema
├── README.md
├── frontend/                # aplicación web
├── services/                # user-service/, metadata-service/, streaming-service/
├── observability/           # librería tecflix_telemetry + stack Docker (Collector, Prometheus, Grafana)
├── load-tests/              # scripts de pruebas de carga
├── tests/                   # pruebas funcionales
└── .github/workflows/       # integración continua
```

- Herramientas: CMake 3.20 o superior, compilador con C++17.
- El equipo desarrolla en **Windows (MSVC) y macOS (clang)**; el CI usará Linux (GCC). El código debe compilar en los tres.
- Docker Desktop para levantar el stack de observabilidad.

## 9. Decisiones pendientes del equipo

- Sistema operativo de las máquinas donde se ejecutará el proyecto en la revisión (preguntar al profesor).
- Biblioteca HTTP común para las tres APIs (propuesta: `cpp-httplib` v0.18.3, la que ya usa el módulo de observabilidad).
- Formato de streaming (MPEG-DASH o HLS) y estructura de los segmentos.
- Balanceador de carga (Nginx, HAProxy o Traefik) y estrategia de réplicas para la versión 2.0.
- Formato estándar de los errores JSON.
- Backend para los traces (por ejemplo Jaeger o Tempo) y, para el CI, cómo instalar Protobuf y libcurl en Windows.