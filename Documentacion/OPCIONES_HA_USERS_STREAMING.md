# Opciones de alta disponibilidad para Users y Streaming

Este documento registra alternativas para resolver los bloqueos de estado encontrados al desplegar dos réplicas de Users y Streaming. No selecciona ni implementa una alternativa. La decisión corresponde al equipo; hasta entonces, la topología parcial de Fase 4 es **provisional, solo valida la herramienta**.

## Users

El servicio guarda usuarios y tres índices B-tree en archivos locales. El repositorio documenta que no es seguro entre hilos; el mutex de `UserService` solo coordina dentro de un proceso. El diario de cuota también mantiene un mapa en memoria y persiste un log local. Evidencia: `Servicios/Servicio_Usuarios/include/tecflix/users/storage/user_repository.hpp:33`, `Servicios/Servicio_Usuarios/include/tecflix/users/domain/user_service.hpp:100`, `Servicios/Servicio_Usuarios/src/storage/binary_file.cpp:10` y `Servicios/Servicio_Usuarios/src/storage/quota_operation_journal.cpp:108`.

### Alternativa U1: volumen compartido con exclusión interproceso

- **Descripción:** conservar el formato B-tree y `users.dat`; serializar cada operación entre procesos con un lock del directorio y recargar bajo lock los encabezados/estado de los tres índices y del diario antes de operar.
- **Archivos que tocaría:** `Servicios/Servicio_Usuarios/include/tecflix/users/storage/{user_repository,binary_file}.hpp`, `Servicios/Servicio_Usuarios/src/{storage/binary_file.cpp,storage/user_repository.cpp,storage/quota_operation_journal.cpp,btree/page_file.cpp,domain/user_service.cpp}`, tests de storage/repository/service y CMake del servicio para la abstracción de lock.
- **Riesgos:** serialización de operaciones y posible caída de throughput; crash entre archivos sigue requiriendo recuperación coherente; semántica del lock depende del sistema de archivos. No asumir NFS multi-host correcto sin pruebas específicas.
- **Esfuerzo:** alto; afecta todas las operaciones que lean o muten usuarios/cuotas.
- **Efecto en load test:** permitiría que dos procesos locales atiendan login/read-only contra el mismo dataset. La escritura concurrente sería correcta solo tras probar la recuperación e idempotencia bajo procesos múltiples.
- **Dos máquinas:** posible únicamente con filesystem compartido que garantice locks y durabilidad requeridos; no queda garantizado por un volumen Docker local.

### Alternativa U2: repositorio compartido de base de datos

- **Descripción:** implementar un repositorio remoto como fuente de verdad (por ejemplo MongoDB) y mantener la interfaz de dominio; decidir por separado si el B-tree se elimina o se conserva como índice local derivado.
- **Archivos que tocaría:** agregar implementación/fábrica de repositorio bajo `Servicios/Servicio_Usuarios/include/tecflix/users/storage/` y `src/storage/`; adaptar `src/main.cpp`, configuración, CMake/dependencias y pruebas; revisar `Documentacion/SPEC.md` porque hoy exige persistencia binaria y árbol B en disco.
- **Riesgos:** cambiar el requisito académico de persistencia si el B-tree deja de ser fuente de verdad; migración de datos, unicidad de username/email, cuotas, transacciones y operación segura del driver.
- **Esfuerzo:** muy alto; es un cambio de backend y de despliegue, no solo de Compose.
- **Efecto en load test:** ambas réplicas consultarían la misma fuente; se podrían comparar lecturas y escrituras con consistencia compartida.
- **Dos máquinas:** viable con base de datos accesible desde ambas redes; la disponibilidad dependería de la topología y seguridad de esa base.

### Alternativa U3: primario y réplicas de lectura

- **Descripción:** una instancia conserva la autoridad de escritura y publica cambios a réplicas; el balanceador solo envía lecturas a réplicas y enruta operaciones mutantes al primario.
- **Archivos que tocaría:** `user_repository.hpp/.cpp`, `user_service.cpp`, protocolo/API y `api_server.cpp`, configuración de roles, mecanismo durable de replicación y configuración Nginx.
- **Riesgos:** replicación eventual, elección/failover del primario, replay/idempotencia y divergencia durante particiones; el B-tree sigue requiriendo sincronización consistente.
- **Esfuerzo:** muy alto; implica protocolo distribuido y operación del rol primario.
- **Efecto en load test:** read-only podría distribuirse; login puede ser lectura, pero las rutas de escritura y cuotas requerirían verificar el primario. El resultado mediría un modelo distinto al round-robin simple.
- **Dos máquinas:** posible con transporte y almacenamiento durables entre hosts, pero necesita política explícita de failover.

## Streaming

Streaming persiste `upload_state.json` y archivos fuente/segmentos en el storage local, y al arranque carga esos registros a mapas y colas en memoria. Sus mutex y workers son por proceso. Evidencia: `include/tecflix/VideoService.h:79`, `include/tecflix/VideoService.h:86`, `src/VideoService.cpp:69` y `src/FfmpegTranscoder.cpp:27`.

### Alternativa S1: MongoDB para estado y leases; storage compartido para medios

- **Descripción:** guardar registros, estado/ownership y leases de trabajos en MongoDB; almacenar source, manifiestos y segmentos en un volumen compartido u object storage. Los workers reclaman trabajos con una actualización atómica y lease con expiración.
- **Archivos que tocaría:** `include/tecflix/UploadRecordStore.h`, `src/UploadRecordStore.cpp`, `include/tecflix/VideoService.h`, `src/VideoService.cpp`, `CMakeLists.txt`/dependencias del streaming, configuración en `src/main.cpp`, tests de store/recuperación y Compose.
- **Riesgos:** carreras en expiración/renovación de leases, recuperación de workers, atomicidad entre metadata y archivos, índices/retención y operación de Mongo. Mongo y el storage compartido pueden ser SPOF si se despliegan de una sola instancia.
- **Esfuerzo:** muy alto; reemplaza el control-plane local y requiere pruebas de fallos/reinicio.
- **Efecto en load test:** dos instancias compartirían ready/status/ownership; permitiría probar failover de lecturas. La subida sigue fuera del alcance del escenario salvo decisión adicional.
- **Dos máquinas:** viable si ambas acceden a Mongo y al mismo storage de medios con latencia y permisos compatibles.

### Alternativa S2: filesystem compartido con locks y coordinación durable

- **Descripción:** conservar los JSON y segmentos en filesystem compartido, pero proteger por video los cambios de registro, ownership, borrado y reclamación de trabajos con locks interproceso y estados/leases durables.
- **Archivos que tocaría:** `include/tecflix/UploadRecordStore.h`, `src/UploadRecordStore.cpp`, `include/tecflix/VideoService.h`, `src/VideoService.cpp`, tests de concurrencia/recuperación y configuración de almacenamiento.
- **Riesgos:** rename/fsync/locks dependen de la implementación del filesystem; las colas en memoria deben reconstruirse y no duplicar trabajo; NFS u object mounts pueden no ofrecer la semántica asumida.
- **Esfuerzo:** alto a muy alto; el lock debe abarcar las transiciones de estado y efectos externos de cuota/transcodificación.
- **Efecto en load test:** lecturas de ready media pueden balancearse; para medir trabajo concurrente se deben validar leases, fallos y reintentos, no solo respuestas HTTP.
- **Dos máquinas:** posible solo con filesystem compartido que ofrezca locking y consistencia probados; no se presume por usar un volumen local Docker.

### Alternativa S3: réplicas de fixture inmutable para read-only/login

- **Descripción:** iniciar cada instancia con el mismo conjunto ya listo de registros y medios; no permitir cargas, transcodificación, edición ni borrado durante la campaña. Puede usarse una copia idéntica por réplica o un volumen compartido en modo operativo de solo lectura.
- **Archivos que tocaría:** principalmente Dockerfiles, Compose y scripts/documentación de preparación; no requiere cambiar `VideoService` si las instancias se inicializan antes de aceptar tráfico y los fixtures están listos.
- **Riesgos:** no resuelve consistencia ni failover de escrituras; los caches en memoria pueden quedar obsoletos ante cambios; no representa la disponibilidad V2 completa.
- **Esfuerzo:** bajo a medio para campañas read-only; no habilita HA general.
- **Efecto en load test:** permite validar el recorrido actual de lectura y la herramienta contra dos destinos, etiquetando obligatoriamente el resultado como provisional.
- **Dos máquinas:** posible copiando exactamente el fixture a cada host o montando storage compartido; no requiere coordinación de escrituras mientras se mantenga la inmutabilidad.

## Estado de decisión

- **Decisión del equipo en esta tarea:** no se autoriza modificar estado ni persistencia de Users o Streaming.
- **Fase 4 acotada:** Users y Streaming se mantienen en una instancia; Metadata usa dos réplicas y Mongo compartido. La topología dual es **provisional, solo valida la herramienta** y no equivale al requisito de dos instancias por API.
- **Pendiente:** elegir alternativas para Users y Streaming antes de afirmar HA dual o ejecutar una campaña que se presente como comparación completa single/dual.
