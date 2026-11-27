# Línea base de rendimiento — Tecflix V1

Evidencia técnica para el punto **"Mejorar performance" (V2.0)**: medición del
sistema V1 *antes* de optimizar. Los mismos scripts, datos y escenarios se
vuelven a ejecutar después de cada optimización para la comparación.

> Estas son notas técnicas y datos crudos para el equipo. **No son texto para el
> paper**: según el enunciado, el paper debe redactarse sin ayuda de IA.

- **Rama medida:** `improve-performance`, creada desde `V1test` sin cambios de código.
- **Carpeta de resultados:** [`results/baseline-v1/`](results/baseline-v1/)
  (`summary_k6.csv`, `summary_server.csv`, `summary_process.csv` y el JSON/TXT de cada corrida).

## 1. Entorno

| Elemento | Valor |
|---|---|
| Máquina | Laptop AMD Ryzen 7 7730U (8 núcleos / 16 hilos), 15 GB RAM, Windows 11 |
| Ejecución | WSL2 Ubuntu 24.04; las 3 APIs como procesos nativos de Linux |
| Compilación | GCC 13.3, `RelWithDebInfo` (-O2 -g), telemetría OTLP activada |
| MongoDB | 8.0 en Docker Desktop (contenedor `tecflix-mongo`) |
| Observabilidad | OTel Collector 0.120 → Prometheus 3.2 (scrape 15 s) → Grafana 11.5 |
| Generador de carga | k6 1.3.0 en la misma máquina (topología `single`, 1 servidor físico) |

El generador de carga comparte CPU con los servicios. Esto afecta los valores
absolutos, pero no la comparación antes/después, que usa el mismo entorno.

## 2. Datos de prueba (fijos y reproducibles)

Creados con [`scripts/seed_loadtest.py`](scripts/seed_loadtest.py):

- 64 cuentas `loadtest_001` a `loadtest_064` (una por usuario virtual), cada una con su JWT.
- 1 video real de 30 s, 1280x720, generado con FFmpeg, subido y **transcodificado
  por la API de streaming** a HLS (`segment_00001.ts` se usa en la prueba).
- **100 000 documentos** de metadata (70 % públicos, 30 % privados) repartidos
  entre las 64 cuentas, con títulos, temas y etiquetas variados (semilla fija).

El escenario `create` inserta documentos durante la prueba. Por eso corre al
final y sus documentos se borran al terminar, para que todas las corridas vean
el mismo catálogo de 100 000.

## 3. Escenarios

Se ejecutan con [`scripts/run_baseline.sh`](scripts/run_baseline.sh), 60 s por corrida.

| Escenario | Qué hace | Origen |
|---|---|---|
| `journey_think1_vN` | Recorrido de lectura del equipo (7 GET a las 3 APIs: perfil, cuota, catálogo página 1, metadata, estado, manifiesto HLS, segmento), con **1 s de pausa** entre iteraciones. N = 1, 8, 32, 64 | `load-tests/` (rama `Load_Test`) |
| `journey_think0_vN` | El mismo recorrido **sin pausa** (máximo rendimiento). N = 8, 32, 64 | `load-tests/` |
| `login_vN` | `POST /api/auth/login` (PBKDF2) | [`k6/baseline_extra.js`](k6/baseline_extra.js) |
| `register_v8` | `POST /api/users` (PBKDF2 + inserción en árbol B) | `k6/baseline_extra.js` |
| `catalog_vN` | `GET /api/metadata/videos` alternando orden por fecha/título/autor y página 1/página 50 | `k6/baseline_extra.js` |
| `create_vN` | `POST /api/metadata/videos` | `k6/baseline_extra.js` |

## 4. Resultados del cliente (k6)

| Corrida | Usuarios | Req/s | p50 ms | p95 ms | Fallos |
|---|---:|---:|---:|---:|---:|
| journey_think1 | 1 | 6.4 | 0.9 | 178 | 0 % |
| journey_think1 | 8 | 43.0 | 1.0 | 426 | 0 % |
| journey_think1 | 32 | 34.9 | 1.2 | 8 627 | **4.9 %** |
| journey_think1 | 64 | 37.0 | 1.5 | 9 166 | **9.7 %** |
| journey_think0 | 8 | 66.2 | 1.4 | 920 | 0 % |
| journey_think0 | 32 | 51.4 | 1.5 | 3 872 | **3.2 %** |
| journey_think0 | 64 | 36.2 | 2.4 | 8 997 | **9.9 %** |
| login | 8 | 10.8 | 757 | 1 028 | 0 % |
| login | 32 | 12.4 | 2 588 | 3 271 | 0 % |
| register | 8 | 10.6 | 781 | 1 039 | 0 % |
| catalog | 8 | 7.1 | 1 004 | 1 572 | 0 % |
| catalog | 32 | 6.3 | 5 297 | 7 112 | 0 % |
| create | 8 | 4 727.6 | 1.6 | 2.0 | 0 % |
| create | 32 | 7 755.0 | 3.9 | 6.8 | 0 % |

Lectura rápida: **el sistema no escala con la concurrencia**. El recorrido pasa
de 66 a 36 req/s al subir de 8 a 64 usuarios, y a partir de 32 usuarios aparecen
fallos (timeouts de 10 s).

**Advertencia sobre `max` y `p99`:** casi todas las corridas tienen picos de unos
7.5 s y algunas duraciones *negativas* (`min`). Esto es un salto del reloj de
WSL2 (resincronización con Windows), no una petición lenta real. Para comparar
antes y después, usar **p50, p95, req/s, fallos y el promedio del servidor**.

## 5. Resultados del servidor (Prometheus / OpenTelemetry)

Promedio por ruta (`sum/count` del histograma `http_server_request_duration_seconds`):

| Ruta | journey_think0_v8 | journey_think1_v32 |
|---|---:|---:|
| metadata `GET /api/metadata/videos` (catálogo) | **903.6 ms** | **1 464.2 ms** |
| metadata `GET /api/metadata/videos/:id` | 0.80 ms | 4.74 ms |
| streaming `GET /api/videos/:id` | 1.26 ms | 3.46 ms |
| streaming `GET .../index.m3u8` | 1.28 ms | 5.08 ms |
| streaming `GET .../segments/:segment` | 1.24 ms | 2.12 ms |
| users `GET /api/users/:id/quota` | 0.07 ms | 0.06 ms |
| users `GET /api/users/me` | 0.14 ms | 0.12 ms |

Otras rutas: login 812.6 ms (8 usuarios); catálogo 1 084 ms (8 usuarios); creación de metadata 1.37 ms.

**CPU promedio por proceso** (100 % = un núcleo; de `/proc`):

| Corrida | users | metadata | streaming |
|---|---:|---:|---:|
| login_v8 | 793 % | 0 % | 0 % |
| login_v32 | **1 588 %** | 0 % | 0 % |
| register_v8 | 463 % | 0 % | 0 % |
| catalog_v8 / v32 | 0 % | **1.0 % / 1.2 %** | 0 % |
| create_v32 | 0 % | 278 % | 0 % |
| journey_think0_v8 | 2 % | 3 % | 4 % |

La memoria (RSS) se mantuvo estable en todas las corridas: users unos 21 MB,
metadata unos 36 MB y streaming unos 17.5 MB.

## 6. Oportunidades de optimización (ordenadas por impacto medido)

### O1. Catálogo: MongoDB ordena en memoria o recorre toda la colección
- **Evidencia:** el catálogo tarda de 0.9 a 1.5 s, mientras Metadata usa solo ~1 %
  de CPU; el tiempo se va en MongoDB. Con `explain("executionStats")` sobre 100 000 documentos:

  | Consulta | Tiempo | Docs examinados | Plan |
  |---|---:|---:|---|
  | Orden por fecha, página 1 | 129 ms | 70 565 | `OR(IXSCAN, IXSCAN)` → **SORT en memoria** |
  | Orden por fecha, página 50 | 134 ms | 70 565 | igual + `SKIP` |
  | Orden por título, página 1 | 205 ms | **100 000** | **COLLSCAN** → SORT |
  | Orden por autor, página 50 | 192 ms | **100 000** | **COLLSCAN** → SORT |

- **Causa:** el filtro `$or: [{visibility:"public"}, {owner_id: X}]` combinado con
  el orden `{campo, _id}` no lo cubre ningún índice. Los índices existentes no
  incluyen `_id` como desempate. Los de título y autor tienen collation, pero no
  empiezan con `visibility` ni `owner_id`.
- **Idea:** índices compuestos por rama del `$or` que ya entreguen el orden
  (`{visibility, uploaded_at, _id}`, `{owner_id, uploaded_at, _id}` y sus
  equivalentes con collation para título y autor). Así MongoDB puede usar
  `SORT_MERGE` y leer solo `skip + limit` claves.
- **Efecto en todo el sistema:** con 2 000 documentos el recorrido hacía unas
  2 095 req/s. Con 100 000 baja a 66 req/s solo por esta ruta.

### O2. Catálogo: `countDocuments` en cada página
- **Evidencia:** `countDocuments` con el mismo filtro tarda **50 ms** (70 565
  coincidencias) y se ejecuta en *cada* petición de página.
- **Idea:** cubrir el conteo con índices (después de O1), guardarlo en caché por
  filtro con un TTL corto, o pedirlo solo en la primera página. Esta última
  opción requiere acordar el contrato con la web.

### O3. Streaming: solo 8 hilos y conexiones keep-alive inactivas → timeouts
- **Evidencia:** con 32 o 64 usuarios, del 3 % al 10 % de las peticiones fallan
  por *timeout* de 10 s, **todas** en `GET :8083/api/videos/:id` (la primera
  petición a streaming de cada iteración). El servidor **no registra** estas
  fallas (0 errores 5xx en Prometheus), así que el dashboard no las ve.
- **Causa:** `src/main.cpp` fija `ThreadPool(8)`. cpp-httplib dedica un hilo por
  conexión mientras está viva (keep-alive), así que 8 clientes inactivos bloquean
  a los demás. Esto empeora con pausas entre peticiones (`think1`), que es el
  comportamiento de un usuario real.
- **Idea:** dimensionar el pool según la concurrencia esperada y reducir el
  `keep_alive_timeout` y `keep_alive_max_count`. Además, medir la cola o las
  conexiones activas para que el dashboard lo muestre.

### O4. Streaming → Metadata: conexión nueva y consulta en cada petición (incluso por segmento)
- **Evidencia:** Metadata recibe **4 veces** más `GET /api/metadata/videos/:id`
  que iteraciones del recorrido (p. ej. 2 110 frente a 528). El 75 % es tráfico
  interno de streaming, que valida la visibilidad en estado, manifiesto y **cada
  segmento** HLS. `authorize_video()` crea un `httplib::Client` nuevo (conexión
  TCP nueva) cada vez.
- **Idea:** reutilizar conexiones (cliente keep-alive o pool) y guardar en caché
  la visibilidad por video con un TTL corto (pocos segundos), sin cachear accesos
  denegados de forma que se pueda saltar un cambio a privado.

### O5. Usuarios: PBKDF2 con SHA-256 por software (mbedTLS)
- **Evidencia:** login y registro se estancan en **~11-12 req/s** sin importar la
  concurrencia. Users llega a 1 588 % de CPU, todos los hilos ocupados, unos
  0.58 s de CPU por login. El mismo PBKDF2-HMAC-SHA256 de 600 000 iteraciones
  con OpenSSL, que usa las instrucciones SHA de la CPU (`sha_ni` presente),
  tarda **128 ms**: unas 4.5 veces menos.
- **Idea:** calcular PBKDF2 con OpenSSL (`PKCS5_PBKDF2_HMAC`). El resultado es
  **idéntico** (mismo algoritmo, sal e iteraciones), así que los hashes ya
  guardados siguen siendo válidos y **no se reduce la seguridad**. No se debe
  bajar el número de iteraciones.

### O6. Observabilidad: los buckets del histograma no distinguen latencias bajo 25 ms
- **Evidencia:** los límites son `25, 50, 75, 100, 125, 150, 200, 300, 500,
  1000 ms`. Casi todas las rutas responden en menos de 25 ms, así que Grafana
  muestra siempre p50 = 12.5 ms y p95 = 23.75 ms (interpolación dentro del primer
  bucket). Por arriba, todo lo que pasa de 1 s se reporta como 1 s.
- **Idea:** buckets exponenciales de 0.5 ms a 10 s. Esto es necesario para que
  el dashboard sirva como evidencia del antes y después.

### Candidatos secundarios (medir antes de decidir)
- **Metadata:** cada documento pasa por BSON → texto JSON → `nlohmann::json` (y
  al revés al guardar). Crear metadata cuesta unos 0.36 ms de CPU por petición
  (278 % de CPU a 7 755 req/s).
- **Users:** un solo mutex global para todo el repositorio y `flush()` en cada
  escritura. Con la carga medida, perfil y cuota responden en 0.06-0.14 ms, así
  que hoy **no** es un cuello de botella.

## 7. Cómo repetir la medición

```bash
# 1. Servicios arriba
~/tecflix-env/start-all.sh
# 2. (Solo la primera vez) datos de prueba
python3 performance/scripts/seed_loadtest.py --users 64 --catalog 2000
python3 performance/scripts/seed_loadtest.py --catalog-only --catalog 98000 --seed 4207
# 3. Escenarios (unos 25 min) y resumen
performance/scripts/run_baseline.sh <etiqueta>          # p. ej. after-o1
python3 performance/scripts/summarize_results.py performance/results/<etiqueta>
```

Para repetir un solo escenario: `ONLY='^catalog_' performance/scripts/run_baseline.sh <etiqueta>`.
