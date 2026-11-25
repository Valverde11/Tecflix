# SPEC_LOADTEST.md — Especificación de pruebas de carga

Este documento define el alcance y las decisiones para las pruebas de carga de Tecflix. Complementa `SPEC.md` y `SPEC_DASHBOARD.md`; si hay una contradicción, prevalece `SPEC.md`. Es la fuente de verdad para cualquier implementación de load testing que se agregue en prompts posteriores.

**Estado:** implementación inicial en `loadtest/`; revisar `GUIA_LOADTEST.md` para el alcance validado y las limitaciones vigentes.
**Alcance:** carga y estrés de las APIs de Tecflix y comparación de topologías. No define cambios a la lógica de negocio.

## 1. Objetivo

Medir el comportamiento de Tecflix bajo usuarios concurrentes y obtener evidencia reproducible para:

1. Identificar capacidad, latencia, tasa de errores y posibles cuellos de botella.
2. Comparar un despliegue de una instancia con otro de dos instancias detrás de un balanceador.
3. Relacionar resultados con las métricas de `observability/` y las optimizaciones que se implementen.

La sección 4.3 de `SPEC.md` establece la comparación entre una y dos instancias. `SPEC_DASHBOARD.md` propone k6 para producir tráfico y observar el dashboard; se adopta k6 para esta implementación.

## 2. Alcance y límites

### Incluido

- Escenarios de carga para rutas funcionales de las APIs de Usuarios, Metadata y Streaming, según los contratos disponibles.
- Ejecución repetible y parámetros explícitos para duración, concurrencia, destino y topología.
- Medición de latencia, throughput y errores de las solicitudes; correlación con métricas de Prometheus/Grafana cuando el stack esté disponible.
- Comparación equivalente de una instancia frente a dos instancias con balanceador.
- Perfil de capacidad limitado para proteger los entornos de prueba y un escenario separado para medir el login.
- Documentación de condiciones, resultados y limitaciones para que las comparaciones sean interpretables.

### Fuera de alcance inicial

- Cambiar la lógica de las APIs o el sistema de observabilidad para mejorar artificialmente los resultados.
- Probar una instancia o servicio de terceros sin autorización.
- Borrar o alterar datos compartidos del equipo durante una ejecución.
- Fijar objetivos numéricos de latencia, throughput, concurrencia o disponibilidad sin acuerdo explícito.
- Levantar o configurar automáticamente las dos topologías: deben estar desplegadas y preparadas por el equipo.

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
- El perfil de capacidad tiene límites operativos definidos por el propio harness; estos límites controlan el generador y no representan objetivos de rendimiento del sistema.

## 5. Decisiones

- **Herramienta:** k6, por ser la recomendación de `SPEC_DASHBOARD.md` y permitir escenarios repetibles con métricas HTTP.
- **Rutas funcionales de solo lectura:** `/api/users/me`, `/api/users/:id/quota`, `/api/metadata/videos`, `/api/metadata/videos/:id`, `/api/videos/:id`, el manifiesto HLS y un segmento. El harness no sube ni borra videos y no modifica cuentas/cuotas.
- **Autenticación:** usar una cuenta de prueba existente mediante variables de entorno; el mismo token de esa cuenta se reutiliza por VU en el flujo de solo lectura.
- **Datos de streaming:** proporcionar un video conocido, listo y accesible con la cuenta de prueba para validar manifiesto y segmento.
- **Topologías:** el runner de comparación recibe URLs de las tres APIs de una instancia y de dos instancias detrás del balanceador. No crea infraestructura ni presupone que los datos de las máquinas estén replicados.
- **Protección del destino:** por defecto se aceptan destinos locales o de red privada; destinos externos requieren habilitación explícita. Los perfiles tienen límites conservadores y documentados.
- **Ejecución:** manual/local contra un ambiente de prueba autorizado. CI no genera carga contra servicios.
- **Pendiente:** el equipo debe elegir los umbrales de desempeño y la duración/perfil oficial de la campaña que se usará para reportar los límites V2.

No resolver las decisiones pendientes mediante suposiciones silenciosas. Cuando un prompt las aclare, actualizar esta especificación y registrar brevemente la decisión en la memoria.

## Agent instructions

1. Antes de trabajar en load testing, leer esta especificación, su sección **Agent memory**, `SPEC.md` y las instrucciones relevantes de `SPEC_DASHBOARD.md`.
2. Tratar este archivo como la fuente de verdad del alcance de load testing. Si el código o un prompt entra en conflicto con una decisión documentada, señalar la discrepancia y actualizar la especificación de forma coherente.
3. No inventar endpoints, cargas, umbrales, credenciales, topologías ni resultados. Si faltan detalles no bloqueantes, dejar explícita la limitación; si la decisión cambia significativamente el comportamiento, pedir confirmación.
4. Mantener `Agent instructions` estable. Registrar en `Agent memory` las decisiones importantes en una línea breve, con motivo y estado cuando corresponda.
5. Cada vez que un prompt agregue o cambie requisitos, actualizar primero esta especificación y luego mantener sincronizada `GUIA_LOADTEST.md` con lo implementado y lo que siga pendiente.
6. Documentar la diferencia entre lo especificado, lo implementado y lo validado. No presentar una recomendación o plan como funcionalidad existente.
7. Para cambios de código, seguir las convenciones del repositorio, añadir pruebas enfocadas cuando corresponda y ejecutar la validación mínima relevante.

## Agent memory

- `SPEC.md` §4.3 y el PDF del proyecto V2 requieren determinar y documentar límites mediante pruebas concurrentes, comparando uno y dos servidores.
- Se eligió k6 por la recomendación de `SPEC_DASHBOARD.md`; la ejecución se hace manualmente contra ambientes autorizados, no en CI.
- El flujo principal solo lee datos y requiere cuenta de prueba y video ya listo para no mutar cuotas, usuarios ni almacenamiento.
- El runner de comparación usa endpoints ya desplegados; no levanta ni replica APIs, MongoDB, almacenamiento HLS o balanceadores.
- El generador está limitado por defecto; aumentar la carga es decisión deliberada mediante variables de entorno. Los límites de VUs/duración no son metas de rendimiento.
- No se han acordado umbrales de latencia/throughput ni la campaña definitiva para reportar capacidad V2.
