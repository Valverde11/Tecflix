# Tecflix V1 — Guía de terminales, comandos y diagnóstico

**Entorno:** macOS con Zsh · rama de integración `V1test` · raíz del repositorio: `~/Programacion/Tecflix`.

> **Atención:** la numeración de terminales es una convención. Puedes abrir cualquier terminal para cualquier proceso. Mantén **un proceso por puerto** y ejecuta los comandos desde el directorio indicado. No publiques `.env`, JWT, contraseñas, datos de usuarios ni videos de prueba en Git.

## 0. Mapa rápido

| Proceso | Puerto | Directorio de trabajo | Comando de arranque |
|---|---:|---|---|
| Usuarios | 8081 | Raíz Tecflix | `./Servicios/Servicio_Usuarios/build/user-service` |
| Metadata (MongoDB) | 8082 | Raíz Tecflix | `./tecflix-metadata/services/metadata-service/build-mongo/metadata-service` |
| Streaming | 8083 | Raíz Tecflix | `./build/streaming/tecflix-api` |
| Web (Vite) | 5173 | `Aplicacion_Web` | `npm run dev` |
| MongoDB | 27017 | Servicio Homebrew | `brew services start mongodb-community@8.0` |

**Inicio recomendado:** MongoDB → Usuarios → Metadata → Streaming → Web. Detén cada proceso con `Ctrl+C`.

## 1. Terminal de Usuarios — puerto 8081

```bash
cd ~/Programacion/Tecflix
set -a; source .env; set +a
export PORT=8081
# IMPORTANTE: usar siempre el directorio donde se crearon las cuentas.
# Si ya tienes DATA_DIR definido, NO lo reemplaces con mktemp:
echo "DATA_DIR=${DATA_DIR:-NO_CONFIGURADO}"
# Si quedó vacío, define explícitamente la ruta PERSISTENTE correcta antes de arrancar.
./Servicios/Servicio_Usuarios/build/user-service
```

- Cambiar `DATA_DIR` hace que las cuentas y el historial de cuotas parezcan desaparecer.
- `AUTH_SECRET` se comparte entre las APIs. `SERVICE_SECRET` es interno y NO se envía al navegador.
- Comprobación desde otra terminal: `curl -i http://localhost:8081/health`.

## 2. MongoDB — servicio local

```bash
brew services list
brew services start mongodb-community@8.0  # Solo si no está iniciado
mongosh 'mongodb://127.0.0.1:27017' --eval 'db.adminCommand({ ping: 1 })'
```

Resultado esperado del `ping`: `ok: 1`.

Inspección del catálogo (el nombre `videos` se confirmó en nuestras pruebas):

```bash
mongosh 'mongodb://127.0.0.1:27017/tecflix_metadata'
# Ya dentro de mongosh:
show collections
db.videos.find({}, { _id: 1, title: 1, owner_id: 1, size_bytes: 1, visibility: 1 }).pretty()
exit
```

No ejecutes `dropDatabase`, `deleteMany` ni limpiezas manuales para resolver cuotas.

## 3. Terminal de Metadata — puerto 8082, CON MongoDB

```bash
cd ~/Programacion/Tecflix
set -a; source .env; set +a
export PORT=8082
export MONGO_URI='mongodb://127.0.0.1:27017'
export MONGO_DB='tecflix_metadata'
./tecflix-metadata/services/metadata-service/build-mongo/metadata-service
```

El arranque debe indicar **«usando MongoDB»**. Verifica `curl -i http://localhost:8082/health` desde otra terminal. **No uses el binario `build/metadata-service` compilado con MongoDB desactivado**, porque pierde el catálogo al reiniciar.

## 4. Terminal de Streaming — puerto 8083

```bash
cd ~/Programacion/Tecflix
set -a; source .env; set +a
export TECFLIX_PORT=8083
export TECFLIX_ENABLE_QUOTAS=1
export TECFLIX_USER_SERVICE_HOST=127.0.0.1
export TECFLIX_USER_SERVICE_PORT=8081
export TECFLIX_ENABLE_METADATA_AUTH=1
export TECFLIX_METADATA_HOST=127.0.0.1
export TECFLIX_METADATA_PORT=8082
export TECFLIX_FFMPEG="$(command -v ffmpeg)"
# Si YA tienes la ruta persistente correcta, conserva su valor:
echo "TECFLIX_STORAGE=${TECFLIX_STORAGE:-NO_CONFIGURADO}"
# Solo para una prueba AISLADA nueva, si no hay videos que conservar:
# export TECFLIX_STORAGE="$(mktemp -d /tmp/tecflix-stream-XXXXXX)"
./build/streaming/tecflix-api
```

- **Nunca regeneres `TECFLIX_STORAGE` al reiniciar** si quieres conservar videos, operaciones de cuota y propietarios. La variable se conserva en la misma sesión de terminal; si abres otra, vuelve a definir la ruta original.
- Verifica `curl -i http://localhost:8083/health`.
- No pongas `AUTH_SECRET` ni `SERVICE_SECRET` en `Aplicacion_Web/.env`.

## 5. Terminal de React / Vite — puerto 5173

```bash
cd ~/Programacion/Tecflix/Aplicacion_Web
cat .env
npm run dev
```

`Aplicacion_Web/.env` de desarrollo contiene **solo** URLs públicas y modo real:

```dotenv
VITE_USERS_API_URL=http://localhost:8081
VITE_METADATA_API_URL=http://localhost:8082
VITE_STREAMING_API_URL=http://localhost:8083
VITE_MOCK_VIDEO_APIS=false
```

Abre `http://localhost:5173`. Tras editar `.env`, **reinicia Vite** con `Ctrl+C` y `npm run dev`. Es válido ejecutarlo desde cualquier terminal situada en `Aplicacion_Web`.

## 6. Terminal libre de pruebas — salud de los servicios

```bash
cd ~/Programacion/Tecflix
for PORT in 8081 8082 8083; do
  printf 'Puerto %s: ' "$PORT"
  curl -sS -o /dev/null -w '%{http_code}\n' "http://localhost:$PORT/health"
done
curl -I http://localhost:5173/
lsof -nP -iTCP:8083 -sTCP:LISTEN
```

Esperamos HTTP `200` para las tres APIs y para `/` en Vite. Si el navegador reporta `Preflight response ... 404`, revisa CORS/`OPTIONS`, no el tamaño del video.

Preflight real de streaming:

```bash
curl -i -X OPTIONS http://localhost:8083/api/videos \
  -H 'Origin: http://localhost:5173' \
  -H 'Access-Control-Request-Method: POST' \
  -H 'Access-Control-Request-Headers: authorization,content-type'
```

Esperado: `204` y cabeceras `Access-Control-Allow-Origin`, `-Methods`, `-Headers`.

## 7. JWT de una cuenta de prueba

Para prueba de cuántas cuentas hay en el data:
```python
python3 - <<'PY'
from pathlib import Path
import struct

data = Path("data/users.dat").read_bytes()

HEADER_SIZE = 24
RECORD_SIZE = 177

payload_size = len(data) - HEADER_SIZE

print("Tamaño del archivo:", len(data))
print("Posible cabecera:", HEADER_SIZE)
print("Tamaño de cada registro:", RECORD_SIZE)

if payload_size < 0 or payload_size % RECORD_SIZE != 0:
    print("El archivo no coincide con el formato esperado.")
    raise SystemExit(1)

total = payload_size // RECORD_SIZE
print("Registros encontrados:", total)
```

Obtener un token nuevo sin mostrar la contraseña (se conserva solo durante el comando):

```bash
read -r 'TEST_USER?Usuario: '
read -rs 'TEST_PASS?Contraseña: '; echo
TOKEN=$(TEST_USER="$TEST_USER" TEST_PASS="$TEST_PASS" python3 -c '
import json,os
print(json.dumps({"username":os.environ["TEST_USER"],"password":os.environ["TEST_PASS"]}))
' | curl -fsS -X POST http://localhost:8081/api/auth/login \
  -H 'Content-Type: application/json' -d @- | \
  python3 -c 'import json,sys; print(json.load(sys.stdin)["token"])')
unset TEST_PASS
export TOKEN
```

Comprobar:

```bash
curl -i http://localhost:8081/api/users/me -H "Authorization: Bearer $TOKEN"
```

Para extraer el `sub` (solo inspección; **el servidor debe verificar la firma**):

```bash
USER_ID=$(python3 -c '
import os,json,base64
p=os.environ["TOKEN"].split(".")[1]; p += "="*(-len(p)%4)
print(json.loads(base64.urlsafe_b64decode(p))["sub"])
')
echo "USER_ID=$USER_ID"
```

No uses `USERNAME` como variable en Zsh. Un JWT puede expirar; un segundo token de la misma cuenta también tendrá el mismo `sub`.

## 8. Consultar cuota, subir, verificar y eliminar videos

**Cuota** (requiere `USER_ID` que corresponda al token):

```bash
curl -sS "http://localhost:8081/api/users/$USER_ID/quota" \
  -H "Authorization: Bearer $TOKEN"
```

**Tamaño real original en macOS:**

```bash
stat -f%z Entrega_Teatro.mp4
```

**Subida directa** (normalmente la web hace esto y después crea metadata):

```bash
curl -sS -o /tmp/tecflix_upload.json -w 'HTTP %{http_code}\n' \
  -X POST 'http://localhost:8083/api/videos' \
  -H "Authorization: Bearer $TOKEN" \
  -H 'Content-Type: application/octet-stream' \
  --data-binary @Entrega_Teatro.mp4
VIDEO_ID=$(python3 -c 'import json; print(json.load(open("/tmp/tecflix_upload.json"))["id"])')
echo "VIDEO_ID=$VIDEO_ID"
```

**Consultar estado (streaming) y visibilidad (metadata):**

```bash
curl -i "http://localhost:8083/api/videos/$VIDEO_ID" \
  -H "Authorization: Bearer $TOKEN"
curl -i "http://localhost:8082/api/metadata/videos/$VIDEO_ID" \
  -H "Authorization: Bearer $TOKEN"
```

**Manifiesto HLS** (ruta real: sin `/hls/`):

```bash
curl -i "http://localhost:8083/api/videos/$VIDEO_ID/index.m3u8"
```

Público sin token: `200` y texto `#EXTM3U`; privado sin token: `401`. Un usuario distinto no puede borrar video ajeno: `403`.

**Eliminar desde streaming (propietario):**

```bash
curl -i -X DELETE "http://localhost:8083/api/videos/$VIDEO_ID" \
  -H "Authorization: Bearer $TOKEN"
```

Esperado `204`, luego video `404` y cuota liberada. **Si el video también existe en metadata**, la limpieza completa debe eliminar ambos registros; preferir la opción «Eliminar» desde la web, que coordina ambos servicios. No descontar manualmente `usedBytes` mientras el archivo permanezca activo.

## 9. Compilar y probar

### Streaming

```bash
cd ~/Programacion/Tecflix
cmake -S . -B build/streaming -DCMAKE_BUILD_TYPE=Debug -DBUILD_TESTING=ON
cmake --build build/streaming --parallel 3
ctest --test-dir build/streaming --output-on-failure
# Pruebas específicas:
ctest --test-dir build/streaming -R 'StreamingAuthHttp|StreamingQuotaIntegration|QuotaClientHttp|UploadRecordStore' --output-on-failure -V
```

### Usuarios

```bash
cmake -S Servicios/Servicio_Usuarios -B Servicios/Servicio_Usuarios/build \
  -DCMAKE_BUILD_TYPE=Debug -DUSER_SERVICE_BUILD_TESTS=ON
cmake --build Servicios/Servicio_Usuarios/build --parallel 3
ctest --test-dir Servicios/Servicio_Usuarios/build --output-on-failure
```

Encabezado correcto del diario: `#include "tecflix/users/quota_operation_journal.hpp"` (sin `/storage/`).

### Metadata con MongoDB

```bash
cmake -S tecflix-metadata/services/metadata-service \
  -B tecflix-metadata/services/metadata-service/build-mongo \
  -DCMAKE_BUILD_TYPE=Debug -DMETADATA_BUILD_TESTS=ON \
  -DMETADATA_ENABLE_MONGO=ON -DCMAKE_PREFIX_PATH="$(brew --prefix)"
cmake --build tecflix-metadata/services/metadata-service/build-mongo --parallel 3
ctest --test-dir tecflix-metadata/services/metadata-service/build-mongo --output-on-failure
```

### Web

```bash
cd ~/Programacion/Tecflix/Aplicacion_Web
npm test
npm run build
npm run lint
# Prueba de subida en particular:
npm test -- src/features/upload/UploadPage.test.tsx
```

## 10. Git y parches incrementales

```bash
cd ~/Programacion/Tecflix
git branch --show-current  # Esperado: V1test
git status --short
git apply --check nombre_del_parche.patch
git apply nombre_del_parche.patch
git diff --check
git diff --stat
```

No fuerces parches con conflictos. Verifica que `.env` de raíz y `Aplicacion_Web/.env` estén ignorados antes de `git add`:

```bash
git check-ignore -v .env Aplicacion_Web/.env
```

## 11. Diagnóstico / recuperación

| Síntoma | Qué revisar |
|---|---|
| Pantalla blanca | Consola Safari, `VITE_*` de `Aplicacion_Web/.env`, reinicio de Vite |
| Preflight 404 | `OPTIONS /api/videos`, `set_pre_routing_handler`, cabeceras CORS, reiniciar streaming |
| JWT 401 | Token vencido, usuario incorrecto, `AUTH_SECRET` compartido |
| JWT de otro usuario da 200 | Compara `sub` de ambos tokens: podrían pertenecer a la misma cuenta |
| `quota_service_rejected` | Contrato de `operationId` (guiones, **no** `:reserve`), usuario existente, `SERVICE_SECRET` |
| Cuota no aumenta | `TECFLIX_ENABLE_QUOTAS=1`, `QuotaClient`, puertos, registros persistidos |
| Video desaparece tras reinicio | Misma ruta `TECFLIX_STORAGE` y metadata compilada con MongoDB |
| Metadata no aparece | Revisar `build-mongo`, `MONGO_URI`, `MONGO_DB`, MongoDB `ping`, JWT |
| Error FFmpeg «Invalid data found» | Es **esperado** si se subió deliberadamente un archivo que no es video |
| Video huérfano / cuota cobrada | Buscar `upload_state.json`, comprobar propietario y `quota_state`; usar `DELETE` de streaming con el JWT del dueño; no manipular cuota a ciegas |

**Buscar registros persistentes de streaming:**

```bash
find "$TECFLIX_STORAGE" -name upload_state.json -print
python3 - "$TECFLIX_STORAGE" <<'PY'
import json,pathlib,sys
for p in pathlib.Path(sys.argv[1]).glob('*/upload_state.json'):
    try:
        r=json.loads(p.read_text())
        print(r.get('video_id'),r.get('owner_id'),r.get('size_bytes'),r.get('status'),r.get('quota_state'))
    except (OSError,ValueError) as e:
        print('ERROR:',p,e)
PY
```

## 12. Operaciones inciertas entre web y metadata (Etapa 4.5)

**Regla de seguridad:** si el navegador muestra `metadata_result_uncertain` con un ID, **NO vuelvas a subir el archivo**: un segundo POST completo crea otro video y puede reservar cuota otra vez.

1. Copia el `video_id` que muestra el error.
2. Comprueba desde la API y MongoDB si el registro existe. Reutiliza el mismo token del propietario:

   ```bash
   VIDEO_ID='ID_QUE_MOSTRO_LA_WEB'
   curl -i "http://localhost:8082/api/metadata/videos/$VIDEO_ID" -H "Authorization: Bearer $TOKEN"
   curl -i "http://localhost:8083/api/videos/$VIDEO_ID" -H "Authorization: Bearer $TOKEN"
   ```

3. Si metadata lo creó, conserva el video; no envíes DELETE de streaming. Si metadata confirma que no existe, aún debes descartar una operación en curso antes de compensar.
4. No borres ni reintentes manualmente ante `503`, respuestas de red ambiguas o inconsistencias sin inspeccionar ambos servicios y la cuota.

**Pendiente para completar V1:** reconciliación persistente al recargar/cerrar el navegador, eliminación parcial recuperable entre streaming/metadata, y validación de telemetría real del dashboard.

---

*Documento de referencia para el entorno local que se ha venido probando. Algunas rutas y comandos dependen de los nombres de binarios y variables de tu rama; verifica `git status` y las rutas reales si el equipo las modifica.*
