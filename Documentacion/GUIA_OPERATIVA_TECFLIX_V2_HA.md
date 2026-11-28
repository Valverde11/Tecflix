# Tecflix V2 — Guía operativa del parche de alta disponibilidad

**Fecha:** 28 de noviembre de 2026. **Rama esperada:** `V2_Single_Point_Failure`. **Entornos:** macOS Apple Silicon (`arm64`) y Ubuntu VM (`aarch64`, `192.168.64.2`). **Mac accesible desde Ubuntu:** `192.168.64.1`. **Ruta del proyecto en Ubuntu:** `/home/tecflix/proyectos/Tecflix`.

> **Alcance real.** Este paquete crea una ruta `/ready` para metadata que hace *ping* de MongoDB; agrega un HAProxy nuevo para las tres APIs, pruebas de failover de macOS↔Ubuntu y una carga GET limitada; incorpora compatibilidad de `libpqxx` 7/8. **No instala un replica set MongoDB, no replica PostgreSQL, no comparte archivos de streaming, ni elimina el punto único de fallo del HAProxy o del Mac físico.** Es un laboratorio y una base de código. Las pruebas reales deben ejecutarse en tus máquinas.

## 0. Qué incluye y qué NO modifica

- C++ metadata: `/health` (liveness) sigue respondiendo 200 cuando vive el proceso. **Nuevo `GET /ready`** responde 200 solo cuando `VideoRepository::Ready()` verifica almacenamiento; para MongoDB realiza un `ping` real. Si la BD falla o la consulta arroja excepción responde `503 {"status":"unavailable"}` sin mostrar credenciales. Si el servicio se compiló con almacenamiento en memoria, devuelve 200, por lo que **esa opción no sirve como prueba de Mongo real**.
- C++ usuarios: `#include <pqxx/version>` y alias condicionado a versión. Si ya existe, el aplicador lo reconoce sin duplicarlo.
- Nuevo `observability/failover-lab/v2-multimaquina/` con `compose.yaml` + `haproxy.cfg`. Utiliza `9281/9282/9283` para usuarios/metadata/streaming y `9299` para estadísticas, sin tocar los laboratorios originales en `9081..9083`, `9182` o `9199`.
- Scripts `tecflix_v2_run_metadata.py`, `tecflix_v2_failover_check.py`, `tecflix_v2_mongo_smoke.py`, `tecflix_v2_metadata_continuidad.py`, `tecflix_v2_load_probe.py`.
- No crea ni cambia `.env`, no toca `build/`, no borra datos, no altera configuraciones de telemetría. Usa Python 3 estándar para arrancar las APIs; únicamente el *smoke* con `--verify-mongo` requiere `pymongo` en macOS.

### Tabla de terminales

| Etiqueta | Equipo | Uso y estado deseado |
|---|---|---|
| **MAC-1** | macOS | API metadata A, proceso activo (`:8082`) |
| **MAC-2** | macOS | Docker Desktop / HAProxy (`:9282`) |
| **MAC-3** | macOS | Comandos de pruebas; activar entorno `tecflix-mongo` cuando se necesite `pymongo` |
| **UBU-1** | Ubuntu | Túnel SSH a MongoDB del Mac, proceso SSH activo |
| **UBU-2** | Ubuntu | API metadata B, proceso activo (`:8082`) |
| **UBU-3** | Ubuntu | Diagnóstico (`curl`, `ss`, `df`) sin dependencias adicionales |

**Regla:** cuando una instrucción dice `Ctrl+C`, aplícalo solo a la terminal/proceso indicado. No apagues MongoDB, Docker Desktop o el Mac por accidente. Para la caída de una máquina solo apagaremos **la VM Ubuntu**, nunca el anfitrión macOS en este laboratorio.

## 1. Preparación segura: aplicar el parche en macOS y sincronizar Git

### 1.1 MAC-3 — preparar respaldo y revisar rama

Desde la **raíz del proyecto Tecflix**:

```bash
pwd
git branch --show-current
git status --short
git check-ignore .env Aplicacion_Web/.env infraestructure/postgres-v2/.env
git ls-files -- .env Aplicacion_Web/.env infraestructure/postgres-v2/.env
```

La rama esperada es `V2_Single_Point_Failure`. El último comando **no debería listar ningún `.env` real**. Si lista alguno, **DETENTE** y corrige el seguimiento de secretos antes del push. Nunca pegues tus secretos en el chat o en el repositorio.

No ejecutes `source .env`. El `.env` se parsea como datos, no como script.

Copia el ZIP a `~/Downloads/` y extráelo **fuera del repo**:

```bash
mkdir -p ~/Downloads/tecflix-v2-paquete
unzip -o ~/Downloads/tecflix_v2_paquete_integrado.zip -d ~/Downloads/tecflix-v2-paquete
python3 ~/Downloads/tecflix-v2-paquete/APLICAR.py --project "$PWD"
```

La última orden es **solo simulación**. Si ves `CONFLICTOS`, no apliques el parche: el paquete encontró archivos C++ modificados con contenido distinto al ZIP auditado. Muéstrame `git diff -- <ruta>` sin datos sensibles.

Si la simulación no marca conflictos:

```bash
python3 ~/Downloads/tecflix-v2-paquete/APLICAR.py --project "$PWD" --apply
git diff --check
git status --short
```

Los archivos C++ existentes se respaldan en `~/.cache/tecflix-v2/backups/<fecha>/`, fuera del repo. **El instalador no instala librerías ni ejecuta binarios**. Si el parche ya se aplicó, mostrará `IGUAL`.

### 1.2 MAC-3 — compilar y validar la nueva ruta `/ready`

Tu build de macOS ya funciona con MongoDB. Desde la raíz:

```bash
cmake --build build/metadata-mongo --parallel 2
ctest --test-dir build/metadata-mongo --output-on-failure
python3 -m unittest discover -s tests -p 'test_tecflix_v2_tools.py' -v
```

En macOS se espera que el test de metadata incluya tres pruebas nuevas de readiness. Si **no** se compilaron, revisa que CMake tenga `METADATA_BUILD_TESTS=ON`; si hay errores de compilación comparte el primero completo.

No hay que tocar `build/v2-telemetry` de Ubuntu.

### 1.3 MAC-3 — commit/push CON revisión, no `git add .`

**Solo después de las pruebas exitosas en macOS:** verifica diferencias y sube exclusivamente los archivos deseados:

```bash
git diff --stat
git status --short
git add Servicios/Servicio_Usuarios/src/storage/postgres_user_repository.cpp
git add tecflix-metadata/services/metadata-service
git add scripts/tecflix_v2_*.py
git add observability/failover-lab/v2-multimaquina
git add Documentacion/GUIA_OPERATIVA_TECFLIX_V2_HA.md
git diff --cached --stat
git diff --cached --check
git diff --cached --name-only
```

**Revisa la lista**: no debe contener `.env`, ejecutables, `build/`, ZIPs, datos ni secretos. El `.env.example` puede conservarse en el repo como plantilla sin contraseñas reales.

```bash
git commit -m "feat(v2): readiness MongoDB y laboratorio failover multimaquina"
git push origin V2_Single_Point_Failure
```

**Solo si tu equipo coordina esa rama**: avisa al resto antes de subir cambios concurrentes. Si tu Mac muestra modificaciones de otras personas o conflictos, no fuerces el push ni uses `git reset --hard`.

### 1.4 UBU-3 — conservar el cambio local de libpqxx antes del pull

En Ubuntu, desde la raíz:

```bash
cd /home/tecflix/proyectos/Tecflix
git status --short
git stash push -m "Respaldo pqxx local antes del parche V2" -- Servicios/Servicio_Usuarios/src/storage/postgres_user_repository.cpp
git branch --show-current
git pull --ff-only origin V2_Single_Point_Failure
grep -nE 'pqxx/version|PQXX_VERSION_MAJOR|PostgresRow' Servicios/Servicio_Usuarios/src/storage/postgres_user_repository.cpp
```

Si la rama no coincide, **detente**. `git stash` no guarda cambios no versionados, por lo que revisa otros archivos señalados por `git status`. **No hagas `git stash pop`** tras el pull: el mismo cambio debería venir incluido desde GitHub. Conserva el stash como respaldo temporal.

### 1.5 UBU-3 — compilar incrementalmente (poco espacio)

Ubuntu tiene ~2,2 GB disponibles. No repitas descarga de driver y no uses `--clean-first`:

```bash
cd /home/tecflix/proyectos/Tecflix
df -h /
cmake --build build/metadata-mongo --parallel 2
ldd build/metadata-mongo/metadata-service | grep 'not found' || true
```

El build actual de Ubuntu es `Release` con metadata y Mongo habilitados; no cambies esa configuración si funcionó previamente. **No borres** `build/v2-telemetry`, `build/streaming`, ni la instalación `~/.local/tecflix-mongo-cxx`. Después de validar el ejecutable es posible recuperar espacio eliminando **solo los artefactos de compilación del driver** en `~/tecflix-deps`, pero hazlo tras comprobar con `du -sh` que no contiene fuentes editadas que necesites. No hace falta limpiarlo ahora.

## 2. Arranque de servicios (una terminal por proceso)

### 2.1 MAC-1 — MongoDB debe estar encendido

En macOS, confirma que responde (como ya hiciste):

```bash
mongosh --quiet --eval 'db.runCommand({ping:1}).ok'
```

Debe mostrar `1`. Si no, arranca MongoDB del modo habitual con el que ya funcionaba. **No alteres `bindIp` ni abras el puerto 27017 a la VM**; usamos SSH.

### 2.2 UBU-1 — túnel cifrado hacia MongoDB (mantener abierto)

En Ubuntu (no macOS), terminal exclusiva:

```bash
ssh -o ExitOnForwardFailure=yes -N -L 127.0.0.1:27018:127.0.0.1:27017 juan_rcx@192.168.64.1
```

Deja el proceso SSH vivo; es normal que no imprima nada. Para comprobar desde **UBU-3**:

```bash
ss -ltn | grep 27018
```

Si el túnel falla, usa `ssh -v juan_rcx@192.168.64.1`; si SSH pide verificar huella, confírmala contra la clave de tu Mac. **No** copies el comando multilínea con `\` y espacios (ese fue el error anterior).

### 2.3 UBU-2 — iniciar metadata Ubuntu

Desde la raíz en Ubuntu, sin virtualenv, sin exportar variables y **con el túnel abierto**:

```bash
cd /home/tecflix/proyectos/Tecflix
python3 scripts/tecflix_v2_run_metadata.py --machine ubuntu --check
python3 scripts/tecflix_v2_run_metadata.py --machine ubuntu
```

El ejecutable usará `PORT=8082`, `MONGO_DB` y `AUTH_SECRET` del `.env` local. El script transforma la dirección `MONGO_URI` al túnel `127.0.0.1:27018`, preservando credenciales y opciones compatibles. No imprime secretos. Deja **UBU-2** abierta. Para detener **solo la API**, usa `Ctrl+C` en UBU-2; el túnel en UBU-1 queda abierto.

### 2.4 MAC-1 — iniciar metadata macOS (otra terminal si Mongo usa servicio)

Desde la raíz del proyecto en macOS, sin virtualenv necesario:

```bash
python3 scripts/tecflix_v2_run_metadata.py --machine macos --check
python3 scripts/tecflix_v2_run_metadata.py --machine macos
```

La salida de `--check` incluye una **huella corta SHA256 de `AUTH_SECRET`**. **Compárala localmente** con la huella mostrada por Ubuntu: deben coincidir. No envíes el secreto. Si no coinciden, corrige tus `.env` antes de probar autorización. Deja **MAC-1** abierta.

### 2.5 MAC-3 — verificar directo ambos servidores y readiness

```bash
curl -i http://127.0.0.1:8082/health
curl -i http://127.0.0.1:8082/ready
curl -i http://192.168.64.2:8082/health
curl -i http://192.168.64.2:8082/ready
```

Todos deben devolver HTTP 200. `/health` muestra que el proceso está vivo; `/ready` además que su almacenamiento responde. Si `/ready` da `404`, hay un **ejecutable antiguo**: recompila en esa máquina. Si da `503`, revisa MongoDB y el túnel. No inicies HAProxy hasta que ambos `/ready` sean 200.

## 3. HAProxy multi-API (nuevo y separado)

### 3.1 MAC-2 — revisar puertos y levantar Docker

**Docker Desktop debe estar activo**. Desde la raíz en macOS:

```bash
docker ps --filter publish=9282 --format 'table {{.Names}}\t{{.Ports}}'
docker compose -f observability/failover-lab/v2-multimaquina/compose.yaml config
docker compose -f observability/failover-lab/v2-multimaquina/compose.yaml run --rm --entrypoint haproxy haproxy-v2 -c -f /usr/local/etc/haproxy/haproxy.cfg
docker compose -f observability/failover-lab/v2-multimaquina/compose.yaml up -d
docker compose -f observability/failover-lab/v2-multimaquina/compose.yaml ps
```

El laboratorio nuevo expone **solo en el loopback de macOS**:

| Servicio | Entrada HAProxy | Backend Mac | Backend Ubuntu |
|---|---|---|---|
| Usuarios | `127.0.0.1:9281` | Mac `:8081` | VM `192.168.64.2:8081` |
| Metadata | `127.0.0.1:9282` | Mac `:8082` | VM `192.168.64.2:8082` |
| Streaming | `127.0.0.1:9283` | Mac `:8083` | VM `192.168.64.2:8083` |
| Estadísticas | `127.0.0.1:9299/stats` | — | — |

**IMPORTANTE:** si usuarios y streaming no están arrancados en ambos lados, **sus backends figurarán DOWN**; el proxy puede continuar funcionando para metadata. No hagas pruebas de redundancia de usuarios si una instancia sigue usando archivos locales independientes, ni de streaming si no comparten archivos transcodificados.

Si aparece `could not resolve host.docker.internal`, comprueba el bloque `extra_hosts` incluido. Si Docker no alcanza `192.168.64.2`, desde **MAC-3** `curl http://192.168.64.2:8082/ready` puede funcionar aunque Docker tenga una ruta de red distinta: conserva los logs para diagnóstico.

### 3.2 MAC-3 — balanceo macOS ↔ Ubuntu, ambos activos

```bash
curl -i http://127.0.0.1:9282/ready
python3 scripts/tecflix_v2_failover_check.py --service metadata --phase both
```

Resultado esperado: `RESULTADO: OK` con peticiones repartidas entre `macos` y `ubuntu`. Esto no escribe a MongoDB.

### 3.3 MAC-3 — CRUD real pasando por el balanceador

**Solo en Mac:** activa tu virtualenv Python para que `pymongo` esté disponible y carga `.env` como datos usando `python-dotenv`:

```bash
source "$HOME/.venvs/tecflix-mongo/bin/activate"
python3 -m dotenv -f .env run -- python3 scripts/tecflix_v2_mongo_smoke.py --url http://127.0.0.1:9282 --verify-mongo
```

El probador usa usuario/video ficticios; crea un documento temporal y procura limpiarlo. No ejecutes contra una base de producción. El resultado exitoso exige `RESULTADO: OK` y que se hayan comprobado las escrituras reales en MongoDB.

### 3.4 Caída del **proceso** macOS → Ubuntu sigue disponible

- **MAC-1**: `Ctrl+C` únicamente en la terminal de metadata A. Mantén MongoDB, Docker y el Mac encendidos. En Ubuntu conserva **UBU-1** y **UBU-2** activas.
- **MAC-3**:

```bash
python3 scripts/tecflix_v2_failover_check.py --service metadata --phase only-ubuntu
python3 -m dotenv -f .env run -- python3 scripts/tecflix_v2_mongo_smoke.py --url http://127.0.0.1:9282 --verify-mongo
```

Se espera que la API de Ubuntu atienda todas las nuevas peticiones tras la detección de falla. No se garantiza cero errores durante el instante de corte o para operaciones en vuelo.

### 3.5 Recuperación de macOS

- **MAC-1**: ejecuta de nuevo `python3 scripts/tecflix_v2_run_metadata.py --machine macos` y déjala activa.
- **MAC-3**:

```bash
python3 scripts/tecflix_v2_failover_check.py --service metadata --phase restore
```

Se espera que ambas instancias vuelvan a ser atendidas.

### 3.6 Caída de la **VM Ubuntu completa** → macOS sigue disponible

- Confirma antes que **MAC-1**, MongoDB y Docker siguen encendidos.
- **Ubuntu**: guarda cualquier trabajo y apaga **la VM** por el menú del hipervisor o desde UBU-3 con `sudo poweroff`. Esto corta la API y el túnel (UBU-1/2), así que no podrás seguir usando Ubuntu hasta reiniciarla.
- **MAC-3**:

```bash
python3 scripts/tecflix_v2_failover_check.py --service metadata --phase only-macos
python3 -m dotenv -f .env run -- python3 scripts/tecflix_v2_mongo_smoke.py --url http://127.0.0.1:9282 --verify-mongo
```

Esto demuestra tolerancia a la **caída de la VM** desde el Mac, NO supervivencia a la caída física del Mac anfitrión.

### 3.7 Recuperar Ubuntu tras apagarla

1. Enciende la VM. **UBU-3**: `ip -br -4 addr`, confirma que sigue `192.168.64.2`.
2. **UBU-1**: reabre el túnel SSH del paso 2.2.
3. **UBU-2**: arranca metadata del paso 2.3.
4. **MAC-3**:

```bash
curl -i http://192.168.64.2:8082/ready
python3 scripts/tecflix_v2_failover_check.py --service metadata --phase restore
```

Si la VM cambió de IP, modifica el backend `ubuntu` en `haproxy.cfg` y el argumento `--ubuntu-host` del checker, y recrea el contenedor. No inventes IPs.

## 4. Prueba adicional: detección de dependencia MongoDB (readiness)

**Ejecútala solo después del resto, con ambos procesos de metadata activos.** Esta prueba no destruye datos, pero simula pérdida de base de datos desde Ubuntu y debe hacerse fuera de una demostración de usuario real.

1. **UBU-1**: corta **solo el túnel SSH** con `Ctrl+C`; no apagues metadata Ubuntu.
2. **MAC-3**: espera algunos segundos, luego comprueba:

```bash
curl -i http://192.168.64.2:8082/health
curl -i http://192.168.64.2:8082/ready
python3 scripts/tecflix_v2_failover_check.py --service metadata --phase only-macos
```

Esperado: Ubuntu `/health` 200 y `/ready` **503** (podría tardar hasta el `serverSelectionTimeoutMS` del driver); HAProxy sirve solo por Mac. Esto valida que el balanceador diferencia proceso vivo y base disponible. **No lances `--verify-mongo` hasta que MongoDB del Mac esté bien.**

3. **UBU-1**: reabre el túnel SSH; **MAC-3**:

```bash
curl -i http://192.168.64.2:8082/ready
python3 scripts/tecflix_v2_failover_check.py --service metadata --phase restore
```

**Nota:** si MongoDB en el propio Mac falla, las **dos** instancias podrían responder `/ready` 503, dejando HAProxy sin backends disponibles. Eso confirma un SPOF de datos todavía pendiente.

## 5. Primeras pruebas de carga concurrente (no destructivas)

Estas pruebas son una **línea base**, no cumplen por sí solas el requisito completo de encontrar límites reales. Hacen únicamente GET al catálogo público. Mantén MongoDB activo y evita interrumpir la VM durante una medición.

- **Escenario 1: solo macOS**. Deja MAC-1 activa, apaga solo UBU-2 (`Ctrl+C`). En **MAC-3**:

```bash
python3 scripts/tecflix_v2_failover_check.py --service metadata --phase only-macos
python3 scripts/tecflix_v2_load_probe.py --label un-backend --requests 100 --concurrency 10
```

- **Escenario 2: ambos**. Reinicia UBU-2. En **MAC-3**:

```bash
python3 scripts/tecflix_v2_failover_check.py --service metadata --phase both
python3 scripts/tecflix_v2_load_probe.py --label dos-backends --requests 100 --concurrency 10
```

Compara `requests_per_second`, `latency_ms_p50`, `latency_ms_p95`, tasa de error y condiciones del entorno. Los reportes quedan en `load-tests/results/` (ignorados por Git). La mejora no es garantizada: ambos nodos comparten el mismo MongoDB/CPU/host físico.

**No uses este script como prueba de carga de uploads de video**: la concurrencia real, límites y tasas deben medirse posteriormente con escenarios autenticados, múltiples niveles de concurrencia, datos representativos y métricas del dashboard.

## 6. Usuarios y streaming: qué podemos hacer ya y qué NO debemos afirmar

El nuevo HAProxy atiende HTTP funcional por `9281` y `9283` (no incluye las reglas del lab original que bloqueaban cualquier ruta distinta de `/health`). **Pero configurar dos backends no replica su estado**.

- Usuarios: el ejecutable usa `TECFLIX_USER_STORAGE=file` por defecto; **no lo uses** para dos instancias que deban conservar los mismos usuarios. Es necesario PostgreSQL compartido al menos para la primera prueba, más un esquema de replicación/failover de PostgreSQL para quitar SPOF del motor.
- Streaming: el ejecutable usa `TECFLIX_STORAGE` (directorios de videos) y registra estados en disco. Dos procesos sin almacenamiento común pueden devolver videos distintos o perder segmentos al caer uno. Debemos comprobar rutas de subida/reproducción, FFmpeg y almacenamiento compartido/replicado antes de hacer failover real.
- DB: un único MongoDB en macOS es SPOF. Un replica set real necesita **quórum de mayoría** y diseño de nodos/ubicaciones: no lo sustituye el túnel SSH, ni los dos procesos de API. PostgreSQL también necesita protección propia. El proxy único en macOS es SPOF.

**Por ello:** en el panel de HAProxy es normal que `users` o `streaming` figuren DOWN si sus APIs no se han levantado. No intentes declarar esas etapas terminadas solo por el panel.

Cuando **y solo cuando** las tres APIs funcionen con almacenamiento coherente y los tres puertos del balanceador respondan a operaciones reales, la web puede dirigirse a ellos desde `Aplicacion_Web/.env`:

```ini
VITE_USERS_API_URL=http://localhost:9281
VITE_METADATA_API_URL=http://localhost:9282
VITE_STREAMING_API_URL=http://localhost:9283
VITE_MOCK_VIDEO_APIS=false
```

Estos valores son **un ejemplo de laboratorio macOS**, no reemplaces aún el `.env` si usuarios/streaming no están listos. Tras modificar variables `VITE_*`, hay que reiniciar Vite. No se debe subir `Aplicacion_Web/.env` a Git.


## 7. Finalizar el laboratorio de manera limpia

- **MAC-3**: desactiva el virtualenv de Python (opcional): `deactivate`.
- **MAC-2**: detén únicamente este nuevo proxy:

```bash
docker compose -f observability/failover-lab/v2-multimaquina/compose.yaml down
```

- **MAC-1**: `Ctrl+C` en metadata macOS si no la necesitas. MongoDB puede permanecer como servicio del sistema.
- **UBU-2**: `Ctrl+C` en metadata Ubuntu si ya acabaste.
- **UBU-1**: `Ctrl+C` para cerrar túnel, **después** de detener metadata Ubuntu.
- **UBU-3**: `df -h /`. No borres `build/` ni bibliotecas para recuperar espacio por iniciativa propia.

Si aparecen errores:

```bash
# MAC-3: estado y logs del nuevo proxy (sin secretos)
docker compose -f observability/failover-lab/v2-multimaquina/compose.yaml ps
docker compose -f observability/failover-lab/v2-multimaquina/compose.yaml logs --tail=60
# MAC-3: rutas de salud directas
curl -i http://127.0.0.1:8082/ready
curl -i http://192.168.64.2:8082/ready
curl -i http://127.0.0.1:9282/ready
```

Si el error aparece en CMake, comparte **las últimas 30-50 líneas** y el paso donde apareció. Nunca publiques `.env`, la contraseña de PostgreSQL, tokens JWT ni URI Mongo con credenciales.

## 8. Registro de pruebas y siguientes entregables

Anota (tú o tu equipo) cada evidencia con fecha/hora, máquina, configuración, estado de procesos, comando, respuesta y limitación. Prepara capturas del panel `http://127.0.0.1:9299/stats` y las salidas del verificador (con y sin VM). Esta guía y los scripts son material técnico, **no texto para copiar al paper**: el enunciado oficial establece que el paper debe redactarse sin IA y que cada estudiante debe entender el código y justificarlo.

**Lo que resta para eliminar SPOF de verdad:** replicación MongoDB con quórum y datos persistentes, PostgreSQL redundante, almacenamiento de streaming disponible para ambos nodos, balanceador/entrada redundante y pruebas de fallo de máquina independiente (la VM y el Mac comparten hardware), validación end-to-end web y carga/stress representativa.
