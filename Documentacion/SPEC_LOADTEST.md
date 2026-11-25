# SPEC_LOADTEST.md — Especificación de pruebas de carga

Este documento define el alcance y las decisiones para las pruebas de carga de Tecflix. Complementa `SPEC.md` y `SPEC_DASHBOARD.md`; si hay una contradicción, prevalece `SPEC.md`. Es la fuente de verdad para cualquier implementación de load testing que se agregue en prompts posteriores.

**Estado:** especificación inicial; todavía no hay scripts ni configuración de load testing en el repositorio.  
**Alcance:** carga y estrés de las APIs de Tecflix y comparación de topologías. No define cambios a la lógica de negocio.

## 1. Objetivo

Medir el comportamiento de Tecflix bajo usuarios concurrentes y obtener evidencia reproducible para:

1. Identificar capacidad, latencia, tasa de errores y posibles cuellos de botella.
2. Comparar un despliegue de una instancia con otro de dos instancias detrás de un balanceador.
3. Relacionar resultados con las métricas de `observability/` y las optimizaciones que se implementen.

La sección 4.3 de `SPEC.md` establece la comparación entre una y dos instancias. `SPEC_DASHBOARD.md` propone k6 para producir tráfico y observar el dashboard; la herramienta final queda pendiente de confirmación mediante una decisión registrada abajo.

## 2. Alcance y límites

### Incluido

- Escenarios de carga para rutas funcionales de las APIs de Usuarios, Metadata y Streaming, según los contratos disponibles.
- Ejecución repetible y parámetros explícitos para duración, concurrencia, destino y topología.
- Medición de latencia, throughput y errores de las solicitudes; correlación con métricas de Prometheus/Grafana cuando el stack esté disponible.
- Comparación equivalente de una instancia frente a dos instancias con balanceador.
- Documentación de condiciones, resultados y limitaciones para que las comparaciones sean interpretables.

### Fuera de alcance inicial

- Cambiar la lógica de las APIs o el sistema de observabilidad para mejorar artificialmente los resultados.
- Probar una instancia o servicio de terceros sin autorización.
- Borrar o alterar datos compartidos del equipo durante una ejecución.
- Fijar objetivos numéricos de latencia, throughput, concurrencia o disponibilidad sin acuerdo explícito.

## 3. Requisitos

1. El tráfico debe apuntar únicamente a un entorno local o de pruebas autorizado y claramente identificado.
2. Cada escenario debe declarar las rutas que ejercita, los datos requeridos, la duración y la carga aplicada.
3. Los escenarios deben ser seguros de repetir: usar datos de prueba aislados y no depender de registros compartidos o irreversibles.
4. No usar únicamente `/health` como sustituto de carga funcional. El README advierte que las pruebas de desempeño deben ejercitar rutas reales.
5. Comparar topologías bajo condiciones equivalentes: mismo escenario, perfil de carga, datos y configuración relevante.
6. Reportar errores y fallos de configuración explícitamente; una prueba incompleta no debe presentarse como exitosa.
7. No asumir objetivos de rendimiento ni criterios pass/fail hasta que el equipo los acuerde.
8. Mantener fuera del control de versiones credenciales, secretos, videos, resultados voluminosos y datos sensibles.

## 4. Criterios de aceptación

- Los scripts y sus instrucciones permiten reproducir cada escenario en un entorno autorizado.
- Se puede distinguir qué topología se probó y qué configuración se usó.
- Los resultados incluyen al menos latencia, throughput, errores, perfil de carga y duración.
- Se puede comparar una instancia con dos instancias detrás del balanceador sin cambiar el escenario.
- La documentación explica limitaciones y no atribuye causalidad a métricas que solo sean correlativas.
- Ningún umbral de aprobación se considera acordado hasta quedar escrito en esta sección y en la memoria del agente.

## 5. Decisiones pendientes

- Herramienta de carga (k6 es la opción sugerida por `SPEC_DASHBOARD.md`; Locust y JMeter aparecen como alternativas en `SPEC.md`).
- Rutas concretas y datos de prueba por escenario.
- Entorno/topología automatizada para las dos configuraciones comparadas.
- Perfil de usuarios, duración y criterios cuantitativos de aceptación.
- Si las pruebas se ejecutan localmente, en CI o en ambos.

No resolver estas decisiones mediante suposiciones silenciosas. Cuando un prompt las aclare, actualizar esta especificación y registrar brevemente la decisión en la memoria.

## Agent instructions

1. Antes de trabajar en load testing, leer esta especificación, su sección **Agent memory**, `SPEC.md` y las instrucciones relevantes de `SPEC_DASHBOARD.md`.
2. Tratar este archivo como la fuente de verdad del alcance de load testing. Si el código o un prompt entra en conflicto con una decisión documentada, señalar la discrepancia y actualizar la especificación de forma coherente.
3. No inventar endpoints, cargas, umbrales, credenciales, topologías ni resultados. Si faltan detalles no bloqueantes, dejar explícita la limitación; si la decisión cambia significativamente el comportamiento, pedir confirmación.
4. Mantener `Agent instructions` estable. Registrar en `Agent memory` las decisiones importantes en una línea breve, con motivo y estado cuando corresponda.
5. Cada vez que un prompt agregue o cambie requisitos, actualizar primero esta especificación y luego mantener sincronizada `GUIA_LOADTEST.md` con lo implementado y lo que siga pendiente.
6. Documentar la diferencia entre lo especificado, lo implementado y lo validado. No presentar una recomendación o plan como funcionalidad existente.
7. Para cambios de código, seguir las convenciones del repositorio, añadir pruebas enfocadas cuando corresponda y ejecutar la validación mínima relevante.

## Agent memory

- La necesidad inicial proviene de `SPEC.md` §4.3: simular usuarios concurrentes y comparar una instancia con dos detrás de un balanceador.
- `SPEC_DASHBOARD.md` sugiere k6 y el uso de Grafana como evidencia; la herramienta todavía no está aprobada para esta implementación.
- Al crear esta especificación no existen scripts ni configuración de load testing identificados en el repositorio; por eso esta versión documenta requisitos, no capacidades ya disponibles.
- No se han definido rutas de carga, límites numéricos, perfiles de concurrencia ni criterios cuantitativos de éxito; no asumirlos.
