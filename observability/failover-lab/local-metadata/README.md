# Tecflix V2 — Laboratorio local de HAProxy para Metadata

Este laboratorio se agrega sin reemplazar `observability/failover-lab/haproxy.cfg` ni
los `.env`. Se usa en macOS + Docker Desktop, con dos instancias **en el mismo Mac**.

## Prerrequisitos

1. Docker Desktop en marcha.
2. MongoDB en marcha, con `MONGO_URI` y `AUTH_SECRET` de desarrollo configurados.
3. Metadata A en puerto 8082 y metadata B en puerto 8182, compiladas con `METADATA_ENABLE_MONGO=ON`.
4. Los puertos `9082` y `9099` no están utilizados por otro HAProxy.

Ejemplo de inicio para cada servicio (terminales separadas en la raíz del proyecto):

```sh
source "$HOME/.venvs/tecflix-mongo/bin/activate"
python3 -m dotenv -f .env run -- env PORT=8082 ./build/metadata-mongo/metadata-service
# en otra terminal: usar PORT=8182
```

## Corrección de resolución de nombre en Docker Desktop

En este Compose se ha añadido `extra_hosts: ["host.docker.internal:host-gateway"]`.
El mapeo se hace dentro del contenedor de HAProxy y no altera el DNS del Mac.
Si habías levantado la versión anterior, recrea el contenedor (no basta reiniciarlo):

```sh
docker compose -f observability/failover-lab/local-metadata/compose.yaml up -d --force-recreate
```

Para comprobar el mapeo incluso si HAProxy no puede arrancar:

```sh
docker compose -f observability/failover-lab/local-metadata/compose.yaml run --rm --entrypoint sh haproxy-metadata -c 'grep host.docker.internal /etc/hosts'
```

El nombre debe mostrar una IP; no compartas archivos `.env` ni tokens de autenticación.

## Comandos de laboratorio (desde la raíz)

```sh
docker compose -f observability/failover-lab/local-metadata/compose.yaml run --rm --entrypoint haproxy haproxy-metadata -c -f /usr/local/etc/haproxy/haproxy.cfg
docker compose -f observability/failover-lab/local-metadata/compose.yaml up -d
docker compose -f observability/failover-lab/local-metadata/compose.yaml logs --tail=50
python3 scripts/tecflix_haproxy_local_check.py --phase both
```

Comprueba también tráfico con autenticación y escritura real **a través del proxy**:

```sh
source "$HOME/.venvs/tecflix-mongo/bin/activate"
python3 -m dotenv -f .env run -- python3 scripts/tecflix_mongo_smoke.py \
  --url http://127.0.0.1:9082 --verify-mongo
```

Apaga únicamente metadata A en su terminal (Ctrl+C). Espera unos segundos y ejecuta:

```sh
python3 scripts/tecflix_haproxy_local_check.py --phase only-b
```

Para probar la API funcional mientras A está caída, repite el `tecflix_mongo_smoke.py`
contra el puerto `9082`. Luego reinicia A y prueba:

```sh
python3 scripts/tecflix_haproxy_local_check.py --phase restore
```

Consulta los servidores UP/DOWN en http://127.0.0.1:9099/stats.

## Finalizar

```sh
docker compose -f observability/failover-lab/local-metadata/compose.yaml down
```

**Alcance:** demuestra health checks y distribución/failover de nuevas peticiones
cuando uno de los **procesos** de metadata cae. No demuestra supervivencia a
la caída del Mac, de MongoDB ni del contenedor HAProxy, ni reintentos de
solicitudes en curso. El `/health` actual puede responder 200 aunque la BD
no esté disponible; esa limitación se resolverá después.
