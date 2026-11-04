# Explicación del código — API de usuarios y Aplicación web

Este documento explica en prosa qué hace cada parte del código generado y por qué se hizo así. Sirve para preparar la defensa: cualquier integrante debe poder explicar cualquier pieza.

---

## API de usuarios (`services/user-service/`)

### U0.1 — Base del proyecto con CMake

**Archivos:** `CMakeLists.txt`, `CMakePresets.json`, `cmake/Dependencies.cmake`, `src/main.cpp`.

**Qué hace.** Define cómo se compila el servicio. `CMakeLists.txt` declara el proyecto en C++17 y crea el ejecutable `user-service`. `CMakePresets.json` guarda la configuración de compilación (generador Ninja, modo Debug, carpeta `build/`) para que todos usen exactamente los mismos comandos: `cmake --preset default` y `cmake --build --preset default`.

**Warnings como errores.** La librería de interfaz `user_service_warnings` agrupa las banderas de compilación: `/W4 /WX` en MSVC y `-Wall -Wextra -Wpedantic -Werror` en GCC/clang. Cualquier advertencia detiene la compilación. Esto evita que se acumulen malas prácticas, que cuestan 5 puntos cada una. Se aplica solo a nuestro código, no a las librerías externas.

**Dependencias.** `cmake/Dependencies.cmake` usa `FetchContent`: CMake descarga cada librería desde GitHub en una versión fija la primera vez que se configura.
- **cpp-httplib v0.62.0**: servidor HTTP en un solo header. Es la misma versión que usa streaming-service, para no tener dos versiones en el sistema.
- **nlohmann/json v3.12.0**: lectura y escritura de JSON.

Se descartó vcpkg porque el vcpkg incluido en Visual Studio 2026 no reconoce su propio compilador (toolset v145). FetchContent no requiere instalar nada extra, y funciona igual en Windows, macOS y Linux (CI).

Se apagan las opciones de OpenSSL, zlib, brotli y zstd de httplib: no se usan en V1.0, y dejarlas encendidas haría que la compilación dependa de lo que tenga instalado cada máquina.

### U0.3 + U0.5 — Configuración por entorno y pruebas con GoogleTest

**Archivos:** `include/tecflix/users/config.hpp`, `src/config.cpp`, `include/tecflix/users/environment.hpp`, `src/environment.cpp`, `tests/CMakeLists.txt`, `tests/config_test.cpp`.

**Librería `user_service_core`.** Todo el código del servicio, excepto `main.cpp`, se compila en esta librería. El ejecutable y las pruebas la enlazan, así las pruebas usan exactamente el mismo código que corre en producción.

**`Environment`.** Lee una variable de entorno del proceso. En Windows usa `_dupenv_s`, porque MSVC marca `std::getenv` como inseguro y, con warnings como errores, no compilaría. `_dupenv_s` reserva memoria con `malloc`; el `std::unique_ptr` con `std::free` la libera aunque ocurra una excepción (RAII).

**`Config`.** Reúne la configuración del servicio:

| Variable | Valor por defecto | Uso |
|---|---|---|
| `PORT` | `8081` | Puerto HTTP |
| `DATA_DIR` | `data` | Carpeta de los archivos binarios |
| `CORS_ORIGIN` | `http://localhost:5173` | Origen permitido de la web |

- **Inyección de dependencias.** `FromEnvironment` recibe una función `EnvironmentReader` que resuelve las variables. En producción se pasa `Environment::Get`; en las pruebas, un mapa falso. Así se prueba sin modificar el entorno real.
- **Fallar temprano.** Si `PORT` no es un número o está fuera de 1–65535, se lanza `std::invalid_argument` y el servicio no arranca. Es mejor que usar un puerto por defecto en silencio y que nadie encuentre el servicio.
- **Constructor privado.** Solo se crea un `Config` por medio de las fábricas, lo que garantiza que siempre esté validado.

**Pruebas.** GoogleTest v1.17.0 por FetchContent. `gtest_discover_tests` registra cada prueba en CTest. Se ejecutan con `ctest --preset default`. `gtest_force_shared_crt` evita un error de enlace en Windows por mezclar runtimes de C.

### U0.6 — Servidor HTTP y `GET /health`

**Archivos:** `include/tecflix/users/api_server.hpp`, `src/api_server.cpp`, `include/tecflix/users/health_controller.hpp`, `src/health_controller.cpp`, `src/main.cpp`, `tests/api_server_test.cpp`.

**`HealthController`.** Registra `GET /health`, que responde `200` con `{"status":"ok"}`. Es el contrato que exige el dashboard (SPEC sección 5): Prometheus consulta esta ruta para saber si el servicio está vivo. El JSON se arma con nlohmann/json en vez de escribir el texto a mano, para que el formato siempre sea válido.

**`ApiServer`.** Es dueño del `httplib::Server` y registra las rutas de cada controlador. Cada grupo de rutas vive en su propio controlador (salud, usuarios, autenticación…), y el servidor solo los junta. Así cada clase tiene una sola responsabilidad.
- **No se puede copiar** (constructor de copia borrado): un servidor que escucha un puerto es un recurso único.
- **`Bind` separado de `Listen`.** Primero se abre el puerto y después se empieza a atender. Esto permite que `main` informe si el puerto está ocupado antes de quedarse escuchando, y que las pruebas pidan un puerto libre cualquiera (`BindToAnyPort`) para no chocar con otros servicios.
- **`WaitUntilReady`.** Las pruebas escuchan en otro hilo. Si `Stop()` llegara antes de que `Listen()` empezara, el hilo nunca terminaría. Esperar a que el servidor esté listo elimina esa condición de carrera.

**`main.cpp`.** Lee la configuración, abre el puerto y escucha en `0.0.0.0` (todas las interfaces, necesario para que otras máquinas lo alcancen en V2.0). Cualquier error de arranque se reporta y termina con código `1`, para que un script o Docker detecte la falla.

**Pruebas.** `ApiServerTest` levanta el servidor real en un puerto libre y le hace peticiones con el cliente de httplib. Verifica que `/health` responda 200 con el JSON correcto y que una ruta inexistente dé 404. Son pruebas de integración HTTP: prueban el servicio como lo verá la web.

### U1.1 + U1.2 — Registro de usuario de tamaño fijo y su formato binario

**Archivos:** `include/tecflix/users/storage/` (`storage_error.hpp`, `byte_codec.hpp`, `user_record.hpp`), `src/storage/` (`byte_codec.cpp`, `user_record.cpp`), `tests/byte_codec_test.cpp`, `tests/user_record_test.cpp`.

**Por qué tamaño fijo.** El enunciado pide guardar los usuarios en un archivo binario y buscarlos con un árbol B en disco. Si todos los registros miden lo mismo, la posición del registro número `i` se calcula directamente como `encabezado + i × tamaño`, sin recorrer el archivo. El árbol B solo necesita guardar ese número `i` para llegar al usuario con un único acceso a disco.

**Layout del registro (177 bytes):**

| Campo | Bytes | Nota |
|---|---|---|
| `id` | 8 | entero sin signo |
| `username` | 32 | texto rellenado con `\0` |
| `email` | 64 | texto rellenado con `\0` |
| `password_hash` | 32 | hash de la contraseña (nunca texto plano) |
| `salt` | 16 | sal aleatoria por usuario |
| `quota_bytes` | 8 | cuota fija |
| `used_bytes` | 8 | espacio usado |
| `created_at` | 8 | segundos desde 1970 (UTC), con signo |
| `deleted` | 1 | borrado lógico: 1 = borrado |

**`ByteWriter` / `ByteReader`.** Convierten valores a bytes y de vuelta, con un formato que no depende de la máquina:
- **Little-endian explícito.** Cada entero se escribe byte por byte, del menos significativo al más significativo, con desplazamientos de bits. Escribir el `struct` en crudo (`reinterpret_cast`) dependería del compilador (relleno entre campos, orden de bytes). Así, un archivo creado en Windows se lee igual en Linux, que es el sistema del CI.
- **Cadenas en campos fijos.** Se rellenan con `\0` hasta el tamaño del campo. Si el texto no cabe, se lanza `std::invalid_argument` en vez de recortarlo en silencio. Al leer, el texto termina en el primer `\0`.
- **Lectura segura.** Si se intenta leer más allá del final, se lanza `StorageError`: los datos están incompletos o corruptos.
- **`ByteReader` es dueño de sus bytes** (los recibe por valor y los mueve). Al principio guardaba una referencia `const Bytes&`, y eso causó un bug: `ByteReader reader(file.ReadAt(...))` leía un buffer temporal que ya se había destruido (referencia colgante, comportamiento indefinido). Copiar unos pocos bytes cuesta casi nada y elimina ese tipo de error por completo.
- Se reutilizarán para serializar los nodos del árbol B.

**`UserRecord`.** Es un `struct` porque es un objeto de datos puro, sin reglas propias. Las validaciones de negocio (formato del correo, unicidad del username) irán en `UserService`. `kSerializedSize` se calcula a partir de las constantes de cada campo, así que si cambia un tamaño, todo lo demás se ajusta solo. `operator==` usa `std::tie` para comparar todos los campos; las pruebas lo usan para verificar que escribir y leer devuelve exactamente el mismo registro.

**`StorageError`.** Es una excepción propia para errores de almacenamiento. Separarla de `std::invalid_argument` permite que la capa HTTP responda 500 (falla del servidor) en un caso y 400 (datos inválidos) en el otro.

### U1.3 – U1.5 — Archivo binario de usuarios (`UserDataFile`)

**Archivos:** `include/tecflix/users/storage/user_data_file.hpp`, `src/storage/user_data_file.cpp`, `tests/user_data_file_test.cpp`, `tests/temp_directory.hpp`.

**Formato del archivo `users.dat`:**

```
[ encabezado 24 bytes ][ registro 0 ][ registro 1 ] ... [ registro N-1 ]
  magic   8 B  "TFUSRDAT" — identifica el archivo
  versión 4 B  1 — permite cambiar el formato en el futuro
  tamaño  4 B  177 — tamaño de cada registro
  N       8 B  cantidad de registros
```

**Índice del registro.** Cada usuario se identifica por su índice: 0, 1, 2… Su posición en bytes es `24 + índice × 177` (`OffsetOf`). El árbol B guardará `clave → índice`, así que encontrar un usuario cuesta lo que cuesta buscar en el árbol más un único acceso al archivo de datos. Como el borrado es lógico (flag `deleted`), un registro nunca se mueve y su índice nunca cambia; por eso el árbol no tiene que actualizarse cuando se borra el dato.

**Operaciones:**
- `Append`: escribe el registro al final y después actualiza el contador del encabezado.
- `ReadAt` / `WriteAt`: leen o sobrescriben el registro de un índice. Si el índice no existe, lanzan `std::out_of_range`.
- El constructor abre el archivo, o lo crea vacío con su encabezado (y las carpetas que falten) si no existe.

**Validaciones al abrir** (todas lanzan `StorageError`):
- archivo menor que el encabezado;
- número mágico distinto: se abrió un archivo que no es de usuarios;
- versión o tamaño de registro distintos: el formato cambió;
- archivo más corto de lo que dice el contador: se perdieron registros (truncado).

**Tolerancia a caídas.** En `Append`, el registro se escribe **antes** que el encabezado. Si el programa se cae entre las dos escrituras, el contador sigue siendo el anterior: el registro a medias queda fuera del archivo y el próximo `Append` lo sobrescribe. Por eso se aceptan bytes **de más** al final, pero no bytes **de menos**. La prueba `IgnoresIncompleteRecordFromInterruptedAppend` simula ese caso.

**`flush()` en cada escritura.** Asegura que los datos lleguen al sistema operativo antes de responder al cliente. Después de cada lectura o escritura se revisa el estado del stream; si falló, se limpia con `clear()` para que las operaciones siguientes no queden bloqueadas, y se lanza `StorageError`.

> **Actualización (refactor U2.2):** la lectura y escritura de bajo nivel (`seek`, `read`, `write`, `flush`, `clear`) se movió a la clase `BinaryFile`, que comparten `UserDataFile` y el archivo del árbol B. Ver "Refactor — `BinaryFile`".

**No copiable.** La clase es dueña del `std::fstream`. Dos copias escribiendo el mismo archivo lo corromperían.

**Pruebas.** `TempDirectory` crea una carpeta temporal por prueba y la borra al terminar (RAII). Se prueba:
- escribir 500 registros, cerrar, reabrir y leerlos todos (persistencia);
- sobrescribir uno sin tocar los demás;
- índices fuera de rango;
- archivos corruptos, truncados o sin encabezado;
- la caída simulada a mitad de un `Append`.

### U2.1 — Clave del árbol B y layout del nodo en disco

**Archivos:** `include/tecflix/users/btree/btree_key.hpp`, `btree_node.hpp`, `src/btree/btree_key.cpp`, `btree_node.cpp`, `tests/btree_key_test.cpp`, `tests/btree_node_test.cpp`.

**Repaso: qué es un árbol B.** Es un árbol de búsqueda balanceado pensado para disco. Cada nodo guarda muchas claves ordenadas (no solo una, como un árbol binario), y todas las hojas están a la misma profundidad. Con grado mínimo `t`:
- cada nodo tiene como máximo `2t − 1` claves;
- cada nodo, salvo la raíz, tiene como mínimo `t − 1` claves;
- un nodo interno con `k` claves tiene `k + 1` hijos. El hijo `i` contiene las claves que están entre `clave[i−1]` y `clave[i]`.

Como cada nodo ocupa una página de disco y tiene muchas claves, el árbol es muy bajo. Con `t = 32`, un millón de usuarios cabe en unos 4 niveles, así que una búsqueda lee unas 4 páginas.

**`BTreeKey`.** Es una clave de 32 bytes que se compara byte a byte (`std::array` ya trae ese orden). Usamos una sola clase de árbol para los dos índices:
- **Índice por id:** el número se guarda en **big-endian**, con el byte más significativo primero. Así el orden de los bytes coincide con el orden numérico. En little-endian, 256 (`00 01…`) quedaría antes que 1 (`01 00…`).
- **Índice por username:** el texto se rellena con `\0`, que es el byte más pequeño. Así "ana" queda antes que "ana2", como en un diccionario. Se rechaza un texto con `\0` dentro, porque sería ambiguo con el relleno.

**`BTreeNode`.** Es el nodo en memoria mientras se procesa una operación:
- `keys`: las claves ordenadas;
- `values`: el índice del registro en `users.dat`, uno por clave;
- `children`: el número de página de cada hijo (vacío si es hoja);
- `page`: la página donde vive el nodo.

**Layout en disco.** Siempre ocupa el mismo tamaño, sin importar cuántas claves tenga:

```
is_leaf 1 B | key_count 4 B | keys[2t−1] × 32 B | values[2t−1] × 8 B | children[2t] × 8 B
```

Las posiciones no usadas se escriben en cero. Con `t = 32` son 3037 bytes, que caben en una página de 4096 bytes. Al leer se valida que `key_count` no supere el máximo (si lo supera, el archivo está corrupto). La cantidad de hijos se deduce: una hoja tiene 0 y un nodo interno tiene `key_count + 1`.

**`LowerBound`.** Hace búsqueda binaria dentro del nodo (`std::lower_bound`) y devuelve la posición de la primera clave mayor o igual a la buscada. Esa posición sirve para tres cosas: saber si la clave está en el nodo (`HasKeyAt`), a qué hijo bajar, y dónde insertar.

### Refactor — `BinaryFile` (acceso aleatorio compartido)

**Archivos:** `include/tecflix/users/storage/binary_file.hpp`, `src/storage/binary_file.cpp`, `tests/binary_file_test.cpp`; se simplificó `user_data_file.cpp`.

**Por qué.** El archivo del árbol B necesita exactamente las mismas operaciones que `UserDataFile`: abrir o crear un archivo y leer o escribir bytes en una posición. Copiar ese código sería duplicación, que cuenta como mala práctica. Por eso se extrajo a una clase con una sola responsabilidad: acceso aleatorio a un archivo binario.

**Qué hace:**
- **Constructor.** Si el archivo no existe, lo crea vacío (con sus carpetas) y lo recuerda en `WasCreated()`. Así quien lo usa sabe si debe escribir un encabezado nuevo o validar el existente.
- **`ReadAt` / `WriteAt`.** Hacen `seek` a la posición y leen o escriben. Después de escribir hacen `flush()`. Si algo falla, limpian el estado del stream y lanzan `StorageError`.
- **No copiable**, porque es dueña del `std::fstream`.

`UserDataFile` ahora solo se ocupa del formato (encabezado, registros e índices) y delega la E/S en `BinaryFile`. Esto es **composición**: `UserDataFile` *tiene un* `BinaryFile`. No se usó herencia, porque `UserDataFile` no *es un* archivo genérico y no debe exponer `ReadAt(offset)` crudo a quien lo use.

### U2.2 + U2.3 — Archivo de páginas del índice (`PageFile`)

**Archivos:** `include/tecflix/users/btree/page_file.hpp`, `src/btree/page_file.cpp`, `tests/page_file_test.cpp`.

**Qué es.** El árbol B vive **en disco**, en un archivo dividido en páginas de 4096 bytes. Cada nodo ocupa una página y se lee o escribe completo. `PageFile` es la capa que traduce "página número N" a "posición `N × 4096` del archivo". El árbol (siguiente tarea) no sabe nada de bytes ni de posiciones: solo pide y guarda nodos.

```
página 0: encabezado
  magic "TFBTREE1" | versión | grado t | tamaño de clave | página raíz | cantidad de páginas | primera página libre
página 1, 2, ...: nodos o páginas libres
```

**¿Por qué 4096 bytes?** Es el tamaño de bloque típico de los sistemas de archivos y de una página de memoria. Leer un nodo cuesta un único acceso a disco. El grado máximo (`kMaxDegree = 32`) es el mayor cuyo nodo (63 claves, 3037 bytes) cabe en una página. Un `static_assert` lo verifica **en tiempo de compilación**: si alguien cambia los tamaños y deja de caber, el código no compila.

**Página 0 = "sin página".** Como la página 0 siempre es el encabezado, el número 0 nunca apunta a un nodo y sirve como valor nulo (`kNoPage`), igual que `nullptr` en memoria.

**Creación.** Un archivo nuevo arranca con el encabezado y una raíz hoja vacía en la página 1. Así el árbol siempre tiene raíz y no hay que tratar el árbol vacío como caso especial.

**Lista de páginas libres.** Cuando el árbol fusiona dos nodos al borrar, una página queda sin uso. En vez de dejar un hueco en el archivo, `FreePage` la agrega a una **lista enlazada guardada en el propio disco**: la página libre guarda una marca `0xFF` y el número de la siguiente página libre, y el encabezado guarda la primera. `AllocatePage` reutiliza primero una página libre (LIFO, como una pila) y solo agranda el archivo si no hay ninguna. La marca `0xFF` permite detectar el error de leer una página libre como si fuera un nodo, porque un nodo real empieza con 0 o 1.

**Validaciones** (todas lanzan `StorageError`):
- archivo sin encabezado, o con número mágico, versión o tamaño de clave distintos;
- índice abierto con un grado distinto al que se usó al crearlo: los nodos tendrían otro tamaño y se leería basura;
- archivo truncado;
- página 0, una página inexistente, o leer una página libre como nodo.

Además: liberar la raíz es un error de programación (`std::logic_error`), y un grado fuera de 2–32 lanza `std::invalid_argument`.

**Pruebas.** Cubren el archivo nuevo, la persistencia de nodos y de la lista libre tras reabrir, la reutilización de páginas en orden LIFO, las páginas inválidas, los grados inválidos o distintos, un archivo ajeno, y que un nodo de grado máximo quepa en su página.

### U2.4 + U2.5 + U2.7 — Búsqueda, inserción con división y verificación de invariantes

**Archivos:** `include/tecflix/users/btree/btree.hpp`, `src/btree/btree.cpp`, `tests/btree_test.cpp`.

Se sigue el algoritmo de Cormen, Leiserson, Rivest y Stein, *Introduction to Algorithms*, capítulo 18. Conviene tenerlo a mano para la defensa.

**`Search(key)`.** Empieza en la raíz y en cada nodo:
1. hace una búsqueda binaria (`LowerBound`) para encontrar la primera clave mayor o igual a la buscada;
2. si es igual, devuelve su valor (el índice del registro);
3. si no, y el nodo es hoja, la clave no existe;
4. si no, baja al hijo en esa posición, que contiene justo las claves entre las dos claves vecinas.

Lee una página por nivel. Costo: O(log_t n) lecturas de disco.

**`Insert(key, value)`.** Primero busca la clave. Si ya existe devuelve `false` **sin tocar el árbol**: el índice es único (dos usuarios no pueden tener el mismo id ni el mismo username) y una operación rechazada no debe dividir nodos.

La idea central es **dividir en la bajada** (*split proactivo*): mientras baja hacia la hoja, si el hijo al que va a entrar está lleno (`2t − 1` claves), lo divide **antes** de entrar. Así, al llegar a la hoja siempre hay espacio, y nunca hay que volver hacia arriba a corregir padres llenos.

**`SplitChild(padre, i, hijo)`.** El hijo lleno tiene `2t − 1` claves. Se parte en tres:

```
hijo lleno (t = 2):   [ 10 | 20 | 30 ]
                            ↓ la mediana (20) sube al padre
padre:          [ ... | 20 | ... ]
                     /       \
            hijo [10]         hermano nuevo [30]
```

- las `t − 1` claves menores se quedan en el hijo;
- la clave mediana (posición `t − 1`) sube al padre, en la posición `i`;
- las `t − 1` claves mayores (y sus hijos, si no es hoja) pasan a un hermano nuevo en una página nueva, que se enlaza como hijo `i + 1` del padre.

Se escriben las tres páginas. Después de dividir, se compara la clave con la mediana que subió para saber a cuál de las dos mitades bajar.

**¿Cómo crece el árbol?** Solo cuando la **raíz** está llena: se crea una raíz nueva vacía con la vieja como único hijo, y se divide la vieja. Es la única forma en que aumenta la altura, y por eso **todas las hojas quedan siempre a la misma profundidad**. Un árbol B crece por arriba, no por abajo.

**`CheckInvariants()` (U2.7).** Recorre todo el árbol y verifica cada regla. Si alguna falla, lanza `std::logic_error` indicando cuál y en qué página. Las reglas son:
- ningún nodo tiene más de `2t − 1` claves, y ninguno salvo la raíz tiene menos de `t − 1`;
- la raíz interna no está vacía;
- las claves de cada nodo están estrictamente ordenadas (sin repetidas);
- cada clave está dentro del rango que le corresponde. El hijo `i` debe tener claves mayores que `clave[i−1]` y menores que `clave[i]`; ese rango (`KeyRange`) se pasa hacia abajo en la recursión;
- un nodo interno con `k` claves tiene `k + 1` hijos;
- todas las hojas están a la misma profundidad.

Devuelve el total de claves, y las pruebas lo comparan con lo insertado. Sirve para probar y para depurar: si una operación rompe el árbol, se detecta en el acto.

**Pruebas.** Usan **grado pequeño** (`t = 2` y `t = 3`), porque con nodos de 1 a 3 claves unas pocas inserciones ya provocan divisiones en varios niveles. Se prueba:
- árbol vacío y claves duplicadas;
- la división de la raíz (la altura pasa de 1 a 2);
- 2000 inserciones en orden y en orden aleatorio (con semilla fija para que sea reproducible), verificando invariantes y encontrando cada clave;
- claves de username;
- persistencia: cerrar, reabrir y seguir funcionando;
- que con `t = 32` 2000 claves quepan en 3 niveles como máximo.

**Observación para V2.0.** Cada escritura hace `flush()`, así que insertar miles de claves tarda 1–2 s en las pruebas. Es una oportunidad de optimización medible (por ejemplo, un caché de páginas o agrupar escrituras), que se puede usar para las 5 mejoras de rendimiento que pide V2.0.

### U2.6 — Borrado en el árbol B

**Archivos:** `include/tecflix/users/btree/btree.hpp`, `src/btree/btree.cpp`, `tests/btree_remove_test.cpp`.

**El problema.** Ningún nodo, salvo la raíz, puede quedar con menos de `t − 1` claves. Si se borra una clave de un nodo que ya tiene el mínimo, se rompe el árbol.

**La idea** es simétrica a la inserción: **arreglar en la bajada**. Antes de bajar a un hijo, se garantiza que ese hijo tenga **al menos `t` claves**, una más que el mínimo. Así, cuando se llegue a borrar, siempre se puede quitar una clave sin dejar el nodo por debajo del mínimo, y nunca hay que volver hacia arriba.

Los nombres de los casos son los de Cormen, cap. 18.3. En cada nodo, `RemoveFrom` busca la clave con `LowerBound`.

**Caso 1 — la clave está en una hoja.** Se quita y listo. Es seguro porque al bajar se garantizó que la hoja tiene al menos `t` claves, o es la raíz.

**Caso 2 — la clave está en un nodo interno** (`RemoveFromInternal`). No se puede quitar sin más, porque separa dos hijos. Hay tres sub-casos:
- **2a.** El hijo izquierdo tiene al menos `t` claves: la clave se reemplaza por su **predecesora**, la mayor clave del subárbol izquierdo (`MaxEntry`: bajar siempre por el último hijo hasta la hoja). Luego se borra la predecesora de ese subárbol.
- **2b.** Si no, y el hijo derecho tiene al menos `t` claves: lo mismo con la **sucesora**, la menor del subárbol derecho (`MinEntry`).
- **2c.** Si los dos tienen el mínimo (`t − 1`): se **fusionan** en un solo nodo con la clave en medio y se borra la clave de ese nodo.

**Caso 3 — la clave no está en este nodo** y hay que bajar al hijo `i` (`EnsureChildCanLoseKey`). Si ese hijo tiene solo `t − 1` claves, primero se le da una más:
- **3a. Préstamo (rotación).** Si un hermano vecino tiene al menos `t` claves, presta una pasando por el padre. Por ejemplo, con el hermano izquierdo (`BorrowFromLeft`):

  ```
        padre [ 20 ]                         padre [ 15 ]
        /        \           →               /        \
  izq [10|15]   hijo [30]              izq [10]   hijo [20|30]
  ```

  La clave separadora del padre (20) **baja** al hijo, y la mayor del hermano (15) **sube** al padre. Si no son hojas, el último hijo del hermano pasa a ser el primero del hijo. El orden se mantiene. `BorrowFromRight` es el espejo.
- **3b. Fusión.** Si ningún hermano puede prestar, el hijo se fusiona con un hermano (`MergeChildren`): `(t − 1) + 1 + (t − 1) = 2t − 1` claves, que caben exactas en un nodo. La página del nodo derecho se **libera** y vuelve a la lista de páginas libres. Si la fusión fue con el hermano izquierdo, el rango buscado ahora está en el hijo `i − 1`, y por eso la función devuelve el índice actualizado.

**Cómo baja de altura el árbol** (`ShrinkRootIfEmpty`). Una fusión puede dejar la raíz interna sin claves y con un solo hijo. Ese hijo pasa a ser la raíz y la página vieja se libera. Es la única forma en que el árbol pierde altura: **por arriba, igual que crece**. Por eso las hojas siguen todas a la misma profundidad.

**Borrar una clave que no existe.** Devuelve `false`. Puede haber hecho préstamos o fusiones en la bajada, pero el árbol sigue siendo válido (las pruebas lo verifican).

**Pruebas:**
- **Casos básicos:** hoja raíz, clave inexistente, árbol vacío.
- **Nodos internos:** con `t = 2` e inserción secuencial, las claves múltiplos de 4 quedan en nodos internos; borrarlas pasa por los casos 2a, 2b y 2c.
- **Borrar todo:** 400 claves en orden aleatorio, verificando **todas** las invariantes después de cada borrado; al final la altura vuelve a 1. También 1000 claves borradas en orden ascendente y descendente (`t = 3`).
- **Prueba basada en modelo:** 3000 operaciones aleatorias (60 % insertar, 40 % borrar) se aplican a la vez al árbol y a un `std::map` de la librería estándar, que sirve como "respuesta correcta". Después de cada operación deben devolver lo mismo, y al final contener exactamente lo mismo. Esta prueba encuentra combinaciones de casos que a mano no se le ocurrirían a nadie.
- **Reutilización de páginas:** llenar, vaciar y volver a llenar con 1000 claves no agranda el archivo.
- **Persistencia:** los borrados se mantienen después de reabrir.
