# Bitácora de validación de Tecflix V2

28 de noviembre de 2026 · Mac Apple Silicon · Rama `V2_Single_Point_Failure`

## Auditoría

- El checkout partía de `4a7952a`, con 16 archivos modificados y el índice vacío. Ya incluía la corrección de tasa de éxitos de la sonda, compatibilidad de libpqxx 7/8 y readiness de usuarios. El build local de usuarios se verificó con PostgreSQL activado y la biblioteca de Homebrew.
- Docker Desktop 4.100.1, motor 29.9.3. El proxy original de metadata siguió activo en `9082`. El proxy aislado respondió HTTP 200 en `9282/ready` y `9299/stats;csv`, y HTTP 503 en `9281` y `9283`. Al finalizar, ambos `/ready` directos de metadata seguían en HTTP 200, los puertos directos `8081` y `8083` no aceptaban conexiones y `9282` volvió a quedar cerrado tras retirar el laboratorio.
- El catálogo de `tecflix_v2` en el PostgreSQL activo, publicado en `55432`, mostró `public.users`, `public.quota_operations` y `public.apply_quota_operation(bigint,text,bigint)`. Ejecutar el examen en la base por defecto `postgres` puede dar la impresión de un esquema vacío. Un puerto cerrado no entrega respuesta HTTP. HTTP 503 indica que un proceso o balanceador respondió sin poder servir la solicitud.
- Ubuntu respondió en el puerto SSH, con clave del host registrada. La sesión no interactiva para `tecflix` falló con `Permission denied (publickey,password)`. La prueba de caída de la VM queda pendiente de autorización y acceso operativo.

## Pruebas reproducibles

| Etapa | Archivos y ejecución | Resultado |
|---|---|---|
| MongoDB | `mongo_video_repository.cpp` y `python3 scripts/tecflix_v2_mongo_replica_lab.py --metadata build/metadata-mongo/metadata-service` | Tres miembros con escrituras confirmadas por mayoría. Elección, reanudación de lecturas y escrituras, HTTP 503 sin mayoría y recuperación de datos. |
| PostgreSQL | `infraestructure/postgres-v2/ha-lab/` y `bash scripts/tecflix_v2_postgres_ha_lab.sh` con `psql` y Docker en el PATH. Añade `--graceful` para la parada ordenada. | Con un miembro de etcd fuera, Patroni promovió otra instancia en las dos variantes. La cuota persistió y el antiguo primario volvió como réplica. Al perder la mayoría de etcd, el clúster dejó de ofrecer sesiones de escritura. Las recuperó al restablecer el quórum. El primer ensayo de parada ordenada falló y pasó después de exigir confirmación de réplica síncrona en Patroni. |
| Usuarios, React y streaming | `scripts/tecflix_v2_functional_lab.py`, configuración de Playwright, `VideoService` y `src/main.cpp`. El workflow V2 contiene el arranque de la base desechable y las APIs. En el Mac se verificó también con `--docker-proxy`. | PostgreSQL accesible sin esquema produjo `/health` 200 y `/ready` 503. Pasaron registro, login cruzado y cuota idempotente. HAProxy repartió entre ambas APIs, retiró la que cayó y volvió a incluirla. Chrome completó subida y reproducción HLS. Streaming conservó los bytes de manifiesto y segmento tras el relevo. El respaldo también esperó al FFmpeg huérfano y recuperó la transcodificación interrumpida sin duplicar la cuota. |
| Entradas HTTP | `observability/failover-lab/v2-multimaquina/`. Dos proyectos Compose independientes con puertos publicados distintos. | La segunda entrada siguió devolviendo HTTP 200 para metadata tras detener la primera. Ambas corrían en el Mac. |
| Observabilidad | `observability/stack/` y `observability/telemetry/`. `promtool check config` y `ctest --test-dir build/v2-observability-audit --output-on-failure`. | El segundo Prometheus siguió observando ambos backends de metadata al detener el primero. Una prueba del SDK con dos receptores HTTP verificó que el secundario siguió recibiendo métricas nuevas al cerrar el primario. |
| Integración continua | `.github/workflows/tecflix-v2-ci.yml` y `bash scripts/tecflix_v2_cpp_coverage.sh BUILD_USUARIOS BUILD_METADATA BUILD_STREAMING`, con Clang y flags `-fprofile-instr-generate -fcoverage-mapping`. | Las suites locales pasaron. La ejecución del workflow en GitHub queda pendiente de publicar la rama. |

## Carga de metadata

`bash scripts/tecflix_v2_compare_metadata.sh` reproduce la comparación. Cada repetición alternó 300 GET del catálogo entre dos entradas del mismo HAProxy aislado. La primera apuntaba al Mac y la segunda repartía entre Mac y Ubuntu. La tabla corresponde a la segunda serie, sin compilaciones ni otros laboratorios en paralelo. Sus JSON están en `load-tests/results/v2_tecflix-v2-bench-*.json`. La primera serie quedó en `load-tests/results/v2_audit-*.json`.

| Concurrentes | Backends | Corridas con carga | Mediana HTTP 200/s | Mediana p95 de éxitos, ms | Errores de red |
|---:|---:|---:|---:|---:|---:|
| 5 | 1 | 3 | 1741,08 | 3,92 | 0 |
| 5 | 2 | 3 | 1514,73 | 5,83 | 0 |
| 15 | 1 | 3 | 2019,92 | 11,60 | 0 |
| 15 | 2 | 3 | 1725,45 | 14,38 | 0 |
| 30 | 1 | 3 | 132,17 | 1818,39 | 0 |
| 30 | 2 | 3 | 106,19 | 28,33 | 0 |
| 60 | 1 | 1 | 41,67 | 2553,19 | 17 |
| 60 | 2 configurados | 3 | 62,99 | 580,97 | 70 |

A 5, 15 y 30 concurrentes, la segunda serie obtuvo 150/150 por réplica y repetición. A 30 cayó la tasa de éxitos en ambos diseños. La primera serie ya mostró errores a este nivel. A 60 se omitieron dos corridas de un backend tras recibir HTTP 503 en la comprobación inicial, y en dos corridas con dos backends configurados contestó únicamente Ubuntu. La degradación del entorno ensayado apareció entre 15 y 30 concurrentes. Las pruebas confirman el reparto y la retirada de un backend, con variabilidad suficiente para dejar abierta la comparación de rendimiento. Una corrida sin éxitos aporta cero HTTP 200/s y ninguna latencia de respuestas exitosas.

## Cobertura y límites de aceptación

| Componente | Pruebas unitarias | Cobertura de líneas | Objetivo |
|---|---:|---:|---:|
| React | 310 | 97,72 % | 90 % |
| Usuarios | 274 | 79,50 % | 90 % |
| Metadata | 84 | 77,62 % | 90 % |
| Streaming | 4 suites | 80,46 % | 90 % |
| Observabilidad | 20 | 100 % | 90 % |

La cobertura web fue 96,69 % de sentencias, 92,53 % de ramas y 98,05 % de funciones. Observabilidad midió 93,75 % de ramas. Las pruebas de herramientas Python pasaron 10/10. En el test de cuotas, se sustituyó la suposición de que el puerto contiguo estaba cerrado por un puerto reservado sin escucha. CTest pudo volver a ejecutar las suites de streaming en paralelo.

- **Procesos.** Se probaron fallos de procesos con almacenamiento desechable. Durante una elección las escrituras pueden fallar. En PostgreSQL se prioriza la durabilidad mediante réplica síncrona estricta, que puede bloquear escrituras si falta una réplica confirmada. Una cuota puede reintentarse con el mismo identificador de operación. [Modos de replicación de Patroni](https://patroni.readthedocs.io/en/latest/replication_modes.html).
- **VM y equipo físico.** El Mac y su VM comparten energía, almacenamiento físico e hipervisor. Para validar la pérdida de un equipo hace falta repartir las mayorías de MongoDB y etcd en tres dominios independientes, exponer las réplicas en una red privada y probar la pérdida de cada equipo por separado. El MongoDB del servicio compartido sigue detrás del túnel original y el PostgreSQL compartido sigue en un contenedor.
- **Streaming e ingreso.** El laboratorio local usa exclusión de escritor POSIX y reapertura del mismo almacenamiento. La topología distribuida requiere almacenamiento replicado, exclusión bajo partición y un supervisor para el relevo. Debe repetirse el fallo durante una transcodificación con pérdida de un equipo, además del caso en que se interrumpe la recepción del archivo original. Las dos entradas HAProxy requieren una dirección estable con comprobación de salud, y debe repetirse el flujo React al perder la entrada primaria.
- **Observabilidad.** El despliegue independiente del Collector y Prometheus en cada host evita compartir el mismo proceso de monitoreo. Queda por verificar la continuidad de métricas OTLP de las APIs sobrevivientes y del historial después de perder el equipo.
