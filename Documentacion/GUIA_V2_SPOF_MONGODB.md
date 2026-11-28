# Tecflix V2: guía práctica de SPOF y MongoDB

**Propósito:** continuar la etapa de alta disponibilidad sin confundir servicios duplicados con una plataforma tolerante a fallos. Esta guía es material de trabajo técnico; **no es el paper del curso**.

## Cambios incluidos en este parche

- `observability/failover-lab/haproxy.cfg`: elimina el bloqueo accidental de todas las operaciones salvo `GET /health`, conserva los checks y aumenta los timeouts de subida.
- `observability/failover-lab/compose.yaml`: declara `host.docker.internal:host-gateway` para Docker Engine en Ubuntu.
- `observability/failover-lab/users-api/haproxy.cfg`: deja pasar las rutas normales de usuarios y `OPTIONS`.
- Nuevos `.env.example` en la raíz y `tecflix-metadata/services`, y `SERVICE_SECRET` en la plantilla de usuarios. **No sustituye ni contiene `.env` reales.**
- `scripts/tecflix_mongo_smoke.py`: prueba HTTP de metadata con creación, lectura, confidencialidad, POST idempotente, cuota, edición, listado, eliminación y verificación opcional directamente en MongoDB.

## Qué se necesita entender antes de empezar

1. Un archivo `.env` es **texto de configuración**, no se ejecuta ni se carga por sí solo en estas tres APIs C++. `std::getenv()` busca variables exportadas en el proceso. Vite sí carga `Aplicacion_Web/.env` al iniciar Vite.
2. `AUTH_SECRET` debe ser el mismo en usuarios, metadata y streaming porque todos validan JWT con HS256. `SERVICE_SECRET` protege la comunicación interna de cuotas. **No introducir secretos en archivos `VITE_*`, commits ni capturas de pantalla.**
3. El `.env` de `infraestructure/postgres-v2` solo alimenta Docker Compose en ese directorio, y `user-service` necesita también variables `PGHOST`, `PGPORT`, `PGUSER`, `PGDATABASE`, `PGPASSWORD` cuando trabaja con PostgreSQL.
4. Los `.env.example` son plantillas públicas. **No eliminarlos**; deben mantenerse sin credenciales reales. `.gitignore` ya ignora los `.env` privados.
5. La modificación local de `postgres_user_repository.cpp` para libpqxx es independiente de este parche: **conservarla** (el ZIP original no refleja ese cambio de Ubuntu).

## 0. Respaldar los archivos antes de aplicar el parche

Desde la raíz de tu copia de Tecflix:

```bash
pwd
mkdir -p "$HOME/tecflix-v2-backups"
tar -czf "$HOME/tecflix-v2-backups/haproxy-antes-parche.tar.gz" \
  observability/failover-lab/haproxy.cfg \
  observability/failover-lab/compose.yaml \
  observability/failover-lab/users-api/haproxy.cfg
```

Descomprime `tecflix_v2_parche_spof_mongo.zip` **en la raíz del repositorio**, conservando sus subdirectorios. No contiene binarios, contraseñas ni modificaciones de tu repositorio PostgreSQL local.

## 1. Identificar los programas disponibles en Ubuntu

```bash
command -v cmake; command -v g++; command -v docker; command -v mongosh
command -v python3; command -v curl
find /usr /usr/local -iname '*mongocxx*config.cmake' 2>/dev/null | head -20
```

Si faltan herramientas, recopila la salida. No improvisar cambios de versión del driver ni instalar paquetes sin comprobar tu Ubuntu.

## 2. Cargar el `.env` de la raíz para ejecutar procesos C++

Desde la raíz del repositorio y solo si el archivo `.env` lo creaste tú y confías en su contenido:

```bash
set -a
source ./.env
set +a
# Comprueba presencia sin imprimir los secretos:
python3 - <<'PY'
import os
for k in ('AUTH_SECRET', 'SERVICE_SECRET', 'MONGO_URI', 'MONGO_DB'):
    print(k, 'configurada' if os.getenv(k) else 'FALTA')
PY
```

`source` ejecuta el contenido como comandos de shell: **no lo uses sobre un `.env` recibido de terceros**. Si un valor contiene `&`, espacios u otros símbolos especiales, debe ir entre comillas en el archivo (ej.: `MONGO_URI='mongodb://.../?replicaSet=rs0&retryWrites=true'`). Esta operación solo afecta la terminal actual. Conserva `.env` fuera de Git y evita copiarlo en el chat. Si contiene contraseñas reales, no usar `cat .env` ni `set -x`.

## 3. Compilar metadata con MongoDB verdadero

```bash
cmake -S tecflix-metadata/services/metadata-service -B build/metadata-mongo \
  -DCMAKE_BUILD_TYPE=Debug \
  -DMETADATA_ENABLE_MONGO=ON \
  -DMETADATA_BUILD_TESTS=ON \
  -DTECFLIX_REAL_TELEMETRY=OFF
cmake --build build/metadata-mongo --parallel 2
ctest --test-dir build/metadata-mongo --output-on-failure
```

La configuración `ON` es imprescindible. Si CMake indica que no encuentra `mongocxx`, **no continúes con `OFF` para validar MongoDB**: eso probaría memoria. Comparte el error y la salida del comando de búsqueda de la sección 1, sin secretos. El compilador obtiene otras dependencias mediante CMake FetchContent, que puede necesitar Internet.

En una terminal con las variables cargadas:

```bash
PORT=8082 ./build/metadata-mongo/metadata-service
```

La consola debe informar que usa MongoDB. Si aparece `usando memoria`, no estás validando MongoDB. En otra terminal:

```bash
curl -i http://127.0.0.1:8082/health
```

**Atención:** `/health` solamente verifica que el servidor HTTP responda. No comprueba que MongoDB continúe disponible una vez iniciado el proceso.

## 4. Probar MongoDB de punta a punta

En otra terminal, desde la raíz y con el `.env` exportado (sección 2):

```bash
mkdir -p "$HOME/.venvs"
python3 -m venv "$HOME/.venvs/tecflix-mongo"
source "$HOME/.venvs/tecflix-mongo/bin/activate"
python3 -m pip install pymongo
python3 scripts/tecflix_mongo_smoke.py \
  --url http://127.0.0.1:8082 --verify-mongo
```

Esta prueba crea un único video de prueba, comprueba que esté en `MONGO_DB.videos` y lo elimina al terminar. Si falla, imprime el ID para localizarlo. No borra datos ajenos. Usa un token firmado de prueba, no registra un usuario de verdad; **no reemplaza las pruebas del flujo completo de autenticación**.

Cuando tengas una segunda instancia de **la misma API** con puerto `8182` conectada a la misma base de datos:

```bash
python3 scripts/tecflix_mongo_smoke.py \
  --url http://127.0.0.1:8082 \
  --peer-url http://127.0.0.1:8182 --verify-mongo
```

Para arrancar ambas instancias localmente en terminales distintas usa `PORT=8082` y `PORT=8182`. Que ambas lean los mismos registros demuestra **estado compartido entre APIs**; todavía no demuestra que **MongoDB** resista una caída.

## 5. MongoDB en modo alta disponibilidad

En un despliegue con réplica, `MONGO_URI` debe referirse a las direcciones accesibles de los miembros del conjunto y al nombre del replica set, por ejemplo:

```ini
MONGO_URI=mongodb://mongo-a:27017,mongo-b:27017,mongo-c:27017/?replicaSet=rs0&serverSelectionTimeoutMS=3000&retryWrites=true&w=majority
```

Esto es **un ejemplo de formato**, no una configuración funcional lista para pegar: `mongo-a`, `mongo-b`, `mongo-c`, credenciales, red, TLS, `rs.initiate` y el propio replica set deben configurarse antes. En una única VM solo podrás hacer una simulación; el requerimiento pide separación física/lógica entre máquinas.

Un replica set con **solo dos miembros votantes** no puede mantener mayoría tras la caída de uno. Para tolerar la pérdida de cualquiera de los nodos se suele utilizar un tercer miembro votante independiente (idealmente un tercer nodo con datos; bajo restricciones, un árbitro). Distribuir el tercero de modo que la caída de una máquina no elimine la mayoría es fundamental.

Comprobación solo de lectura, con `mongosh` instalado y `MONGO_URI` exportado:

```bash
mongosh --quiet --nodb --eval 'const c=new Mongo(process.env.MONGO_URI); const a=c.getDB("admin"); printjson(a.runCommand({hello:1})); printjson(a.runCommand({replSetGetStatus:1}).members.map(x=>({name:x.name,stateStr:x.stateStr})))'
```

Si `replSetGetStatus` falla porque es standalone, aún no hay réplica. También puede fallar por permisos insuficientes: ese error no implica por sí solo que falte un replica set. No apagues nodos en una base con datos importantes sin copias de seguridad.

## 6. Probar HAProxy en las dos máquinas (una vez que ambas APIs funcionen)

- En `observability/failover-lab/haproxy.cfg`, reemplazar `192.168.64.2` por la dirección real de la **máquina B**, después de verificar con `curl` que A y B respondan en cada puerto. La configuración incluida supone A alcanzable por `host.docker.internal`.
- Asegurar que streaming se inicie con `TECFLIX_PORT=8083` porque su puerto por defecto en `src/main.cpp` es **8080** y HAProxy busca **8083**.
- Iniciar el balanceador desde `observability/failover-lab` con `docker compose up -d` y `docker compose ps`.
- Verificar rutas funcionales: `curl -i 'http://127.0.0.1:9082/api/metadata/videos?page_size=1'`. Revisar `X-Tecflix-Backend` alternando A y B.
- Detener temporalmente una **API**, no la base de datos, y comprobar que las solicitudes siguen sirviéndose desde la otra. Reiniciar el proceso detenido al terminar. El `/health` por sí solo es evidencia insuficiente.

**Limitaciones pendientes:** la comprobación de salud de HAProxy solo usa la ruta HTTP `/health`, no disponibilidad del almacenamiento. El propio HAProxy es un SPOF si solo hay uno. El almacenamiento local de streaming no es compartido. El PostgreSQL del compose es único. Hay que resolver estos aspectos para certificar la redundancia completa.

## 7. Priorización recomendada

1. **Hacer funcionar `metadata-service` compilado con MongoDB real** y pasar el probador de la sección 4.
2. **Levantar dos instancias de metadata** con una misma base de datos; demostrar que A crea y B lee/edita/elimina.
3. **Configurar el replica set MongoDB** con mayoría disponible después de una caída; repetir pruebas y comparar antes/después.
4. **Resolver almacenamiento compartido y estado** de streaming antes de balancear subidas y reproducción. Evitar usar el mismo volumen de archivos con escrituras concurrentes sin evaluar bloqueos y consistencia.
5. **Completar PostgreSQL HA** para usuarios, readiness de las bases, HAProxy no redundante y ensayos de caída de máquina completa. Añadir pruebas a CI de V2.

### Qué enviar para continuar la depuración

Envía las salidas de CMake (desde la primera línea del error), `ctest --output-on-failure`, la prueba `tecflix_mongo_smoke.py` y el resultado de `mongosh` **sin URI con credenciales, JWT ni contraseñas**. Indica también si tienes ya dos máquinas realmente conectadas, o únicamente una VM Ubuntu; y qué sistema tiene la máquina anfitriona.

