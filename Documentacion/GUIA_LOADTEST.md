# GUIA_LOADTEST.md — Guía de pruebas de carga

Esta guía explica el propósito de las pruebas de carga en Tecflix, qué partes del sistema se pretende ejercitar y cómo interpretar la implementación a medida que se construya. Se actualiza junto con `SPEC_LOADTEST.md` cuando nuevos prompts definan requisitos o cambie el código.

## 1. Estado actual

**Todavía no hay una implementación de load testing en el repositorio.** No existen scripts, perfiles de carga ni comandos de ejecución para las APIs. En consecuencia, esta guía describe el contexto y el diseño acordado hasta ahora; no afirma que las pruebas ya se puedan ejecutar.

La solicitud funcional existente está en `SPEC.md` §4.3: simular usuarios concurrentes y comparar la capacidad de un servidor con dos servidores detrás de un balanceador. `SPEC_DASHBOARD.md` sugiere usar k6 y el dashboard como evidencia, pero la herramienta y los umbrales siguen pendientes.

## 2. Qué componentes se medirán y por qué

| Componente | Responsabilidad actual | Motivo para incluirlo en pruebas |
|---|---|---|
| `user-service` (puerto local 8081) | Autenticación, usuarios y cuotas persistentes | Medir rutas de usuarios/autenticación y observar el efecto de concurrencia sobre operaciones que comparten almacenamiento. |
| `metadata-service` (puerto local 8082) | Catálogo, visibilidad y metadata de videos | Medir consultas de catálogo y operaciones de metadata que representan el uso normal del producto. |
| `streaming-service` (puerto local 8083) | Archivos originales, transcodificación, segmentos HLS y estado del video | Medir acceso al contenido y el trabajo de streaming; separar, cuando se defina el escenario, los costos de subida/transcodificación de la entrega de segmentos. |
| Balanceador y réplicas | La topología de comparación requerida por `SPEC.md` §4.3 | Comprobar si distribuir solicitudes entre dos instancias cambia capacidad, latencia o errores frente a una sola instancia. |
| Observabilidad | Collector, Prometheus y Grafana bajo `observability/` | Relacionar la carga con métricas HTTP y de recursos disponibles; obtener evidencia para comparar ejecuciones. |

Los detalles de despliegue, contratos de APIs y configuración del dashboard deben leerse en el README y en la documentación correspondiente. Esta guía no reemplaza esos contratos.

## 3. Cómo se interpretará una ejecución

Una ejecución debe registrar, como mínimo:

- Topología evaluada y servicios incluidos.
- Rutas y tipo de operación ejercitada.
- Perfil de carga, duración y configuración relevante.
- Latencia y throughput observados, cantidad/tasa de errores y cualquier interrupción.
- Estado de las métricas de observabilidad y limitaciones de los datos.

Para una comparación útil, mantener iguales el escenario, los datos de prueba y el perfil de carga. Cambiar solo la topología que se está comparando, además de las diferencias inevitables que se documenten.

Las métricas ayudan a localizar correlaciones (por ejemplo, mayor latencia junto con más CPU), pero por sí solas no prueban la causa de un cuello de botella. Una respuesta exitosa de `/health` confirma disponibilidad básica, no desempeño funcional. El README también advierte que los histogramas y percentiles dependen de sus buckets.

## 4. Seguridad y repetibilidad

- Ejecutar únicamente contra servicios propios/locales o un ambiente de pruebas autorizado.
- No apuntar scripts a producción ni a sistemas de terceros.
- Usar datos aislados y preservar registros compartidos. Las cargas que suben videos o alteran cuentas requieren datos de prueba desechables o limpieza segura documentada.
- No guardar tokens, secretos, archivos de video ni resultados voluminosos en Git.
- Detener o reportar una prueba si el destino no es el esperado, la carga no está controlada o el sistema muestra efectos no previstos.

## 5. Pendiente para habilitar la guía operativa

Antes de publicar comandos de ejecución o resultados, completar a partir de decisiones explícitas:

1. Herramienta elegida (k6 está sugerida; no confirmada).
2. Rutas funcionales y datos de prueba por escenario.
3. Método reproducible para levantar una instancia y dos instancias con balanceador.
4. Parámetros de carga y límites seguros del entorno.
5. Umbrales de aceptación, si el equipo decide establecerlos.
6. Validaciones disponibles en local y/o CI.

Al implementarse cada elemento, reemplazar el pendiente correspondiente por rutas, comandos y resultados comprobables, manteniendo clara la diferencia entre aprobado, fallido y no ejecutado.

## 6. Mantenimiento con nuevos prompts

Cuando un prompt agregue una decisión o cambie el alcance:

1. Actualizar **Agent memory** y los requisitos/criterios de `SPEC_LOADTEST.md`.
2. Actualizar esta guía con el comportamiento real, la razón de diseño y la forma validada de usarlo.
3. Si aún no hay código, dejarlo indicado como propuesta o pendiente, nunca como funcionalidad existente.
4. Al modificar código, verificar la ruta funcional implicada y actualizar los comandos y resultados que la documentación presenta.
