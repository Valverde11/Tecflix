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

### U2.9 — Índice secundario por username

**Qué problema resuelve.** Al iniciar sesión el usuario da su username, no su id. Sin este índice habría que recorrer todo `users.dat` (O(n)). Con un segundo árbol B, la búsqueda es O(log n), igual que por id.

**Por qué no hay una clase nueva.** `BTree` no sabe qué significan sus claves: compara 32 bytes y devuelve un número. El índice por username es otra instancia de `BTree`, en otro archivo, con claves creadas con `BTreeKey::FromString`. Ambos árboles guardan el mismo valor: el índice del registro en `users.dat`. Mantenerlos sincronizados le corresponde a `UserRepository` (U3.1), así que esta tarea demuestra con pruebas que la pieza sirve para ese uso.

**Garantía en compilación.** `username_index_test.cpp` tiene un `static_assert(UserRecord::kUsernameSize <= BTreeKey::kSize)`. Si alguien agranda el campo del registro y olvida la clave, las pruebas dejan de compilar, en lugar de fallar con usuarios reales.

**Decisión pendiente.** Las claves distinguen mayúsculas: `Ana` y `ana` son usuarios distintos. Si se quiere evitar, se normaliza el username antes de crear la clave (U3.3); el árbol no cambia.

**Pruebas:**
- **Búsqueda y duplicados:** encuentra el registro por username y, ante un duplicado, conserva el primero. Esto será la base de la validación "username único".
- **Claves difíciles:** usernames de 32 bytes que difieren solo en el último byte, y prefijos comunes (`ana` y `ana2`).
- **Renombrar:** es quitar la clave vieja e insertar la nueva con el mismo valor; el registro no se mueve en `users.dat`.
- **Independencia:** cambiar el índice por username no afecta al índice por id, incluso después de reabrir.
- **UTF-8:** `ñandú` queda después de `zeta` porque los bytes se comparan sin signo.
- **Prueba basada en modelo:** 2000 operaciones aleatorias con claves de texto contra un `std::map`. Luego se reabre el archivo y se verifican invariantes y contenido.

**Error útil durante el desarrollo.** La primera versión de la prueba reabría el archivo con el grado por defecto y falló con "El índice fue creado con otro grado". El árbol hizo lo correcto: un archivo creado con un grado no debe interpretarse con otro. Se corrigió la prueba, no el código.

### U3.1 — `UserRepository`: datos e índices como una sola colección

**Qué hace.** Es la única clase que conoce los cuatro archivos: `users.dat`, `users_by_id.idx`, `users_by_username.idx` y `users_by_email.idx` (este último se agregó en U3.3). Ofrece `Create`, `FindById`, `FindByUsername`, `Update` y `Remove`, y las capas superiores no saben que existen árboles ni registros. Cada búsqueda baja por un árbol B (O(log n)) para obtener el índice del registro, y con ese índice lee `users.dat` en una sola operación.

**Cómo se generan los ids.** `id = RecordCount() + 1`. Como el borrado es lógico, `users.dat` nunca pierde registros y el total solo crece: funciona como contador persistente sin agregar campos al archivo, y un id eliminado no se reutiliza. Esto importa porque metadata guarda el id del dueño de cada video: si se reutilizara, un usuario nuevo heredaría los videos de otro.

**Pregunta probable en la defensa: si `id = índice + 1`, ¿para qué el árbol por id?** Hoy esa relación se cumple, pero es un detalle de cómo se genera el id, no una regla del formato. El árbol además sabe qué usuarios están activos: al eliminar uno, su clave sale del árbol. Y es lo que pide el enunciado: las búsquedas se hacen con el árbol B.

**El problema de los varios archivos.** Una operación escribe en varios archivos y el proceso puede caerse entre dos escrituras. No hay registro de transacciones (WAL), así que la defensa es el **orden** de las escrituras. La regla es que `users.dat` manda y que una caída nunca debe permitir dos usuarios alcanzables con el mismo username o email:

| Operación | Orden | Si se cae a la mitad |
|---|---|---|
| `Create` | datos → email → username → id | Tras los datos: registro huérfano que nadie encuentra. Tras el email: ese email queda reservado. Tras el username: el usuario solo se encuentra por username |
| `Update` con cambio de nombre o email | reservar valores nuevos → datos → quitar valores viejos | Los valores viejo y nuevo llevan al mismo registro, pero nadie más puede tomar el nuevo |
| `Remove` | marcar borrado → email → username → id | El usuario ya no aparece en las consultas; lo que falte por limpiar sigue reservado |

¿Por qué el email va primero? Un usuario solo se alcanza por username o por id, y esas claves se escriben después. Así, todo usuario alcanzable ya tiene reservados sus dos valores únicos.

Reservar un valor de más es un defecto menor. Duplicarlo sería grave, porque el login podría entrar a la cuenta equivocada. Estos estados no se reparan solos al arrancar; es la limitación ya conocida de no tener WAL.

**Bug encontrado durante el desarrollo.** La primera versión escribía el índice por id antes que el de username. Si fallaba entre ambos, el usuario existía por id pero su nombre aparecía libre, y otro podía registrarlo. Todas las pruebas pasaban, y el error se encontró al repasar el orden caso por caso. La primera prueba que se escribió tampoco lo detectaba, porque simulaba el estado que deja el orden *nuevo*. La definitiva (hoy `FailedCreateKeepsUsernameAndEmailReserved`) interrumpe `Create` de verdad: siembra el id 1 en el índice para que el `Insert` por id falle. Se comprobó que fallaba con el orden viejo y que pasa con el nuevo. Esta técnica solo puede interrumpir el último paso, así que el orden entre email y username se justifica por el razonamiento de arriba, no por una prueba.

**Validar antes de escribir.** `Create` construye la clave antes de escribir y `Append` serializa antes de tocar el disco. `Update` llama a `Serialize` al principio solo para validar: si el email fuera inválido y eso se descubriera en `WriteAt`, el nombre nuevo ya estaría reservado sin que nadie lo use.

**`Update` no puede eliminar.** Si recibe `deleted = true`, lanza una excepción: marcar el registro sin limpiar los índices dejaría claves apuntando a un usuario eliminado. La única forma de eliminar es `Remove`.

**`WriteStatus` en vez de excepciones.** "No existe" y "valor ocupado" son resultados normales de un repositorio, y `UserService` los convierte en `UserError`. Las excepciones del repositorio quedan para errores de programación (`invalid_argument`) y fallos de disco (`StorageError`, que será un 500).

**Concurrencia.** La clase no es segura entre hilos, y lo dice su comentario. Se protegerá con un mutex en U4.5.

**Pruebas:** 31 casos (incluidos los de U3.3) con `t = 2` para forzar divisiones y fusiones:
- **Operaciones:** ids consecutivos, búsqueda por ambas claves, renombrado, conflicto de nombre, usuario inexistente y persistencia tras reabrir.
- **Datos inválidos:** comprueban que no quede nada escrito.
- **Caídas:** registro huérfano, `Remove` interrumpido y `Create` interrumpido.
- **Carga:** 300 usuarios creados y la mitad eliminados, verificando después las invariantes de los tres árboles.

### U3.2 — `PasswordHasher`: contraseñas con hash y salt

**Qué hace.** `Create` recibe una contraseña y devuelve un hash de 32 bytes y un salt de 16, justo los campos que ya tenía `UserRecord`, así que el formato del archivo no cambió. `Verify` recalcula el hash con el salt guardado y lo compara. La contraseña nunca se guarda: del hash no se puede volver a ella.

**Por qué un hash lento.** Un SHA-256 simple se calcula miles de millones de veces por segundo en una GPU; si alguien robara `users.dat`, probaría millones de contraseñas comunes en segundos. PBKDF2 encadena el hash 600 000 veces (mínimo que recomienda OWASP para HMAC-SHA256), y ese costo se multiplica en cada intento del atacante.

**Por qué salt.** Es un valor aleatorio distinto por usuario. Sin él, dos usuarios con la misma contraseña tendrían el mismo hash, y un atacante podría usar tablas precalculadas. Con él, cada usuario se ataca por separado. El salt no es secreto: se guarda junto al hash.

**Por qué Mbed TLS y no código propio.** El árbol B se implementa a mano porque es lo evaluado. La criptografía es lo contrario: escribirla a mano es una mala práctica conocida. Mbed TLS se descarga con FetchContent como el resto, sin instalar nada, y también cubre el HMAC-SHA256 del token (U3.5). Argon2id resiste mejor los ataques por GPU, pero la opción que lo ofrecía no incluía HMAC-SHA256 y habría exigido una segunda librería.

**Tres detalles de seguridad.**
- **Comparación en tiempo constante** (`mbedtls_ct_memcmp`): una comparación normal termina en el primer byte distinto, y ese tiempo puede filtrar información.
- **Generador aleatorio criptográfico** (`SecureRandom`, CTR_DRBG con entropía del sistema): `rand()` y `mt19937` son predecibles. Es RAII, y se crea uno por llamada para no compartir estado entre hilos.
- **La contraseña se pasa con su longitud**, no como cadena de C: si no, lo que sigue a un byte `\0` se ignoraría. Hay una prueba para esto.

**El `extern "C"` alrededor del include.** `mbedtls/constant_time.h` no tiene ese bloque, a diferencia de los demás headers de la librería. Sin él, C++ busca el símbolo con nombre decorado y el enlazador no lo encuentra (`LNK2019`).

**Costo medido.** Cada hash tarda unos 0,85 s en Release y 3,5 s en Debug (medido en esta máquina). Eso tiene dos consecuencias: el hash debe calcularse **fuera** del mutex del repositorio (U4.5), o un login bloquearía a todos los demás; y limita cuántos logins por segundo aguanta el servicio, algo a considerar en el load-test de V2.0.

**Las iteraciones no se guardan en el registro.** Son una constante, porque agregarlas cambiaría el formato del archivo. Por eso cambiar `kDefaultIterations` invalida las contraseñas existentes.

**Pruebas:** 11 casos, con 1000 iteraciones para que sean rápidas, salvo uno con las 600 000 reales.
- **Vector independiente:** el hash esperado se calculó con `Rfc2898DeriveBytes` de .NET. Sin esto, las pruebas solo compararían Mbed TLS consigo mismo, y un parámetro mal pasado no se notaría.
- **Rechazos:** contraseña distinta, salt distinto, hash con el primer o el último byte cambiado, y otra cantidad de iteraciones.
- **Salt:** la misma contraseña produce salt y hash distintos.
- **Byte nulo:** se usa la contraseña completa aunque contenga `\0`.

### U3.3 — `UserService`, email único y claves sin mayúsculas

**Qué hace.** `UserService` contiene las reglas de negocio: `Register`, `Get`, `Update` y `Delete`. Valida los datos, calcula el hash de la contraseña, asigna la cuota fija de 1 GB y la fecha de registro. El repositorio guarda; el servicio decide qué se puede guardar.

**Email único con un tercer árbol B.** El email ocupa hasta 64 bytes y la clave del árbol, 32. Truncarlo sería un error: dos emails largos con el mismo inicio chocarían. La clave es el **SHA-256 del email en minúsculas**, que produce exactamente 32 bytes, así que el árbol no cambió. No hace falta recuperar el email desde la clave: solo se pregunta "¿ya existe?". Dos emails con el mismo SHA-256 es un caso teórico sin ejemplos prácticos, y el resultado sería solo un falso "email ocupado". Esto no es para proteger el email, porque sigue en claro en `users.dat`: es una forma de hacerlo caber.

**`Ana` y `ana` son el mismo usuario.** Las claves de username y email se crean en minúsculas, pero el registro guarda lo que el usuario escribió. Así se evitan cuentas que se confunden entre sí y se respeta cómo quiere verse. La regla vive en el repositorio porque la unicidad es una propiedad del índice. Por eso `Update` compara **claves** y no textos: pasar de `ana` a `Ana` no toca el índice.

**Solo ASCII y sin `std::tolower`.** Esa función depende del locale del proceso, y una clave debe generarse igual en cualquier máquina. Los usernames además solo aceptan ASCII, lo que evita nombres visualmente idénticos con letras de otro alfabeto.

**Reglas de validación.** `SPEC.md` no las fija, así que son decisión de este componente:

| Campo | Regla |
|---|---|
| Username | 3 a 32 caracteres: letras, dígitos, `_`, `.`, `-` |
| Email | Hasta 64 caracteres, forma `texto@dominio.ext`, ASCII sin espacios |
| Contraseña | 8 a 128 bytes, cualquier carácter |

Se valida en el servidor aunque la web también lo haga, porque cualquiera puede llamar a la API directamente. La contraseña solo se limita en longitud: restringir caracteres la debilitaría. El email no se valida con regex porque una revisión básica basta: la única prueba real de que existe es enviarle un mensaje.

**`UserProfile` sin hash ni salt.** El servicio nunca devuelve `UserRecord`. Si el tipo no tiene esos campos, ningún descuido en la capa REST puede enviarlos en un JSON: lo garantiza el compilador.

**`UserError` con código.** La capa REST traducirá cada código a un estado HTTP en un solo lugar, sin comparar mensajes de texto. Los errores de disco siguen siendo `StorageError`, porque son culpa del servidor y no de la petición.

**`ProfileChanges` con `std::optional`.** Permite distinguir "no cambiar" de "cambiar a vacío". Cuota, espacio usado y fecha no aparecen en el tipo porque el usuario no debe poder modificarlos.

**Reloj inyectado.** `UserService` recibe una función que da la hora, igual que `Config` recibe la que lee el entorno. Las pruebas usan una hora fija y comprueban `created_at` exacto.

**Limitaciones conocidas.**
- `Register` calcula el hash antes de saber si el nombre está libre, así que un registro duplicado gasta casi un segundo de CPU.
- `Update` lee y luego escribe: dos peticiones simultáneas podrían pisarse. Se resolverá en U4.5, junto con la regla de calcular el hash fuera del mutex.

**Pruebas:** 7 de `UserValidator` (límites exactos y caracteres prohibidos) y 17 de `UserService`: registro, datos inválidos sin guardar nada, duplicados sin distinguir mayúsculas, actualización parcial, cambio de contraseña con salt nuevo, y eliminación. Se suman 8 del repositorio, entre ellas emails largos con el mismo inicio y soltar el username reservado cuando el email resulta ocupado.

### U3.4 — Login: `UserService::Authenticate`

**Qué hace.** Recibe username y contraseña, busca el registro con el árbol B por username (O(log n)), verifica la contraseña con `PasswordHasher` y devuelve el `UserProfile`. Si algo falla, lanza `UserError` con `kInvalidCredentials`. No emite el token: eso es de `AuthService` (U3.5), que recibirá este perfil. Así `UserService` no sabe nada de JWT y cada clase tiene una sola responsabilidad.

**Un solo error para todo.** "Usuario inexistente" y "contraseña incorrecta" producen el mismo código y el mismo mensaje. Si fueran distintos, cualquiera podría probar nombres y descubrir qué cuentas existen (enumeración de usuarios), y después atacar solo esas. Por eso no se reutiliza `kUserNotFound`.

**Mismo tiempo de respuesta.** Igualar el mensaje no basta. Verificar una contraseña tarda casi un segundo por las 600 000 iteraciones, y no encontrar al usuario tarda microsegundos: quien mida el tiempo sabría la respuesta igual que si se la dijéramos. Cuando el usuario no existe se ejecuta `Verify` contra un hash y un salt en cero y se descarta el resultado. Solo importa pagar el mismo costo; no hace falta preparar un hash falso real, porque `Verify` tarda lo mismo con cualquier valor.

**Entrada malformada.** Un username con formato inválido o una contraseña fuera de 8–128 bytes se rechazan antes de buscar y sin calcular el hash. Esto no abre la misma fuga de tiempo, porque `Register` y `Update` aplican esas reglas: ninguna cuenta puede tener esos valores, y la respuesta rápida solo revela algo que ya es público. Además hace falta para otra cosa: `FindByUsername` lanza `std::invalid_argument` si el texto no sirve como clave, lo que terminaría en un error 500.

**Se inicia sesión con username, no con email.** Así se decidió al diseñar los índices, y sin distinguir mayúsculas: `ANA` entra a la cuenta `Ana`, porque el índice guarda las claves en minúsculas.

**Limitaciones conocidas.**
- El login no limita los intentos. PBKDF2 encarece cada intento, pero también convierte el login en una forma barata de ocupar la CPU del servidor. Pesa en el load-test de V2.0.
- Cuando el login es correcto o la contraseña es incorrecta el tiempo es el mismo, pero la igualdad con un usuario inexistente es aproximada: leer el registro agrega microsegundos frente a casi un segundo de hash, una diferencia que el ruido de red tapa.

**Pruebas:** 8 casos. Devuelve el perfil, ignora mayúsculas, rechaza contraseña incorrecta, mismo código y mensaje con usuario inexistente, entrada malformada sin `std::invalid_argument`, usuario eliminado, y que el login siga los cambios de username y contraseña.
- **Prueba de tiempo:** compara una contraseña incorrecta con un usuario inexistente usando 50 000 iteraciones. Exige una proporción mayor que 0,25: con el relleno ronda 1, y sin él se midió 0,0004 quitando la línea a propósito. El umbral queda lejos de ambos para que la carga de la máquina no la haga fallar.

### Refactor — `CryptoPrimitives`

`RequireSuccess` (convierte un código de error de Mbed TLS en excepción) y la comparación en tiempo constante vivían dentro de `password_hasher.cpp`. `AuthService` necesita las mismas dos cosas, así que pasaron a una clase compartida antes de escribirlo, para no duplicarlas. Ahí queda también el bloque `extern "C"` que compensa el descuido del header de Mbed TLS: si estuviera en dos archivos, habría que corregirlo en dos lugares. El comportamiento de `PasswordHasher` no cambió.

### U3.5 — `AuthService`: emitir y validar el JWT

**Qué hace.** `IssueToken(perfil)` crea el token después del login y `ValidateToken(texto)` devuelve quién es el usuario, o `std::nullopt` si el token no sirve. Así se autentican las peticiones sin guardar sesiones: el token se valida solo con el secreto, y por eso metadata y streaming pueden comprobarlo sin llamar a este servicio (`SPEC.md` 4.2). En V2.0 esto ayuda: dos instancias no necesitan compartir sesiones.

**Cómo es un JWT.** Son tres partes en Base64Url separadas por puntos: `cabecera.contenido.firma`.
- **Cabecera:** `{"alg":"HS256","typ":"JWT"}`.
- **Contenido:** `sub` (id del usuario), `username` y `exp` (vencimiento, 8 h).
- **Firma:** HMAC-SHA256 de `cabecera.contenido` con `AUTH_SECRET`.

El contenido **no está cifrado**: cualquiera puede leerlo. La firma solo garantiza que nadie lo cambió, por eso el token no lleva datos sensibles. Cambiar un solo carácter del contenido exige recalcular la firma, y sin el secreto no se puede.

**Se valida en este orden, y el orden importa.**
1. **Firma primero.** Nada del contenido se interpreta hasta saber que es auténtico: un atacante no puede hacer que el parser JSON procese datos arbitrarios.
2. **Cabecera exacta.** Este servicio emite un solo tipo de token, así que basta compararla con la constante. El algoritmo nunca se lee del token. El ataque clásico contra JWT es enviar `"alg":"none"` y aprovechar a los validadores que obedecen ese campo; aquí no tiene efecto.
3. **Contenido y vencimiento.** Se revisan los tipos de cada campo. El token vence exactamente en `exp`: en ese segundo ya no sirve (RFC 7519).

**La firma se compara como texto y en tiempo constante.** En tiempo constante por lo mismo que el hash de la contraseña: una comparación normal sale en el primer byte distinto y ese tiempo orienta a quien prueba firmas. Como texto porque 32 bytes ocupan 43 caracteres de Base64 y el último tiene 2 bits que se descartan: `...2HE` y `...2HF` decodifican a la misma firma. Comparar bytes decodificados aceptaría variantes de un mismo token.

**`sub` es texto, no número.** El RFC 7519 define ese campo como texto, aunque contenga `"1"`. Quien lo lea en otros servicios debe convertirlo; hay que avisarlo al equipo.

**`AUTH_SECRET`.** Es la única variable sin valor por defecto: un secreto por defecto estaría en el repositorio y cualquiera podría falsificar tokens. `Config` rechaza que falte o esté vacía, y el servicio no arranca. `AuthService` exige además 32 bytes, la longitud mínima para HS256 según RFC 7518, porque un secreto corto se puede adivinar por fuerza bruta a partir de cualquier token. Esa regla vive en `AuthService` y no en `Config` para que la clase no pueda existir en un estado inseguro; se aplicará al arrancar cuando U4 la cree en `main`. Los mensajes de error nunca incluyen el valor del secreto.

**`ValidateToken` devuelve `optional` y no lanza excepción.** Un token inválido es un caso normal, no un error, y da igual la causa: la respuesta siempre es 401. Por la misma razón `json::parse` se llama sin excepciones.

**`Base64Url` envuelve el Base64 de Mbed TLS.** JWT usa la variante para URLs (`-` y `_`, sin `=`). Se traduce el alfabeto y se quita o repone el relleno, en vez de escribir el algoritmo a mano.

**Limitaciones conocidas.**
- Un token no se puede revocar: si el usuario se elimina o cambia de contraseña, el token sigue siendo válido hasta `exp`. Es el costo de no consultar una base de datos en cada petición. Los endpoints de este servicio igual responderán 404 porque el usuario ya no existe, pero metadata y streaming no se enterarían.
- El `username` del token puede quedar viejo si el usuario lo cambia. El dato confiable es `sub`.
- Todos los servicios comparten `AUTH_SECRET`, así que cualquiera de ellos podría emitir tokens. Con HS256 es inevitable; evitarlo exigiría firmas asimétricas (RS256).

**Pruebas:** 13 de `AuthService`, 5 de `Base64Url` y 1 de HMAC, más 2 nuevas de `Config`.
- **Vectores independientes:** el token esperado se calculó con `HMACSHA256` de .NET y el HMAC se compara con el RFC 4231. Sin esto solo se compararía este código consigo mismo. El Base64 usa los vectores del RFC 4648.
- **Ataques:** contenido alterado, otro secreto, `alg: none` sin firma y con firma válida, y firma que solo cambia en los bits descartados.
- **Vencimiento:** válido un segundo antes de `exp`, inválido justo en `exp`.
- **Contenido inválido con firma correcta:** 14 variantes, como campos ausentes, tipos equivocados o un id que no cabe en 64 bits.
- **`Config`:** las pruebas de puerto ahora incluyen el secreto. Sin eso pasarían por la razón equivocada, porque esperan la misma excepción que produce la falta del secreto.

### U3.6 — Cuota: consultar, reservar y liberar espacio

**Qué hace.** `UserService` suma tres operaciones: `GetQuota` devuelve `{quota_bytes, used_bytes}`, `ReserveSpace` suma bytes al espacio usado y `ReleaseSpace` los resta. Son la lógica detrás de `GET /api/users/{id}/quota` y `POST /api/users/{id}/quota/usage` (`SPEC.md` 4.4), que se crean en U4. El enunciado pide que al alcanzar la cuota no se pueda subir: esa regla vive aquí, en el servidor, porque el bloqueo de la web se puede saltar llamando a la API directamente.

**Llenar la cuota exacta se permite.** Un usuario con 1 GB de cuota puede usar exactamente 1 GB; un byte más se rechaza con `kQuotaExceeded`. Una operación rechazada no escribe nada.

**Por qué se compara con lo disponible.** La condición es `bytes > quota - used`, no `used + bytes > quota`. `bytes` viene de otro servicio y es un entero sin signo de 64 bits: si es enorme, `used + bytes` se pasa del máximo, da la vuelta a un número pequeño y la validación lo aceptaría. En cambio `quota - used` no puede dar la vuelta, porque `used` nunca supera `quota`. Una prueba usa justo un valor cuya suma da 1.

**Liberar de más es un error.** `ReleaseSpace` rechaza con `kReleaseExceedsUsage` si se pide liberar más de lo usado. Restar sin revisar daría la vuelta a un número gigante y dejaría al usuario sin cuota. Ajustar a cero evitaría eso, pero escondería una liberación repetida o un tamaño mal reportado, y el espacio usado quedaría mal sin que nadie lo notara.

**Se usa la cuota del registro, no la constante.** La comparación lee `record.quota_bytes`: la constante `kQuotaBytes` solo decide qué valor recibe un usuario al registrarse.

**`LoadUser`.** "Buscar el usuario o lanzar `kUserNotFound`" se repetía en `Get` y `Update`, y la cuota lo necesitaba tres veces más; por eso se extrajo a un método privado.

**El repositorio no cambió.** `UserRepository::Update` ya guarda el registro completo y, como username y email no cambian, no toca los índices.

**Limitaciones conocidas.**
- Reservar lee y luego escribe: dos subidas simultáneas podrían pasar la validación y perder una suma. Se resuelve en U4.5, con el mutex alrededor de la operación completa.
- Si un servicio reintenta una reserva tras un error de red, los bytes se cuentan dos veces: la operación no es idempotente. Evitarlo exigiría guardar qué videos se contaron, y eso no cabe en el registro actual.

**Pruebas:** 12 de `UserService`: cuota inicial, acumulación persistida, llenado exacto, un byte de más, cuota llena, desbordamiento de enteros, liberación parcial y total, liberar de más sin cambiar nada, independencia entre usuarios, login después de reservar y usuario inexistente.

### U3.7 — Pruebas del servicio con reinicio

**Qué hace.** Las pruebas de `UserService` ya usaban archivos reales en una carpeta temporal, con `TempDirectory`, sin simular el disco. Faltaba lo que distingue a un sistema persistente: que todo siga igual cuando el proceso se reinicia. `EverythingSurvivesARestart` registra dos usuarios, reserva espacio y elimina uno; luego cierra los archivos y los abre de nuevo.

**Qué comprueba después de reabrir.** Que el login funciona (el username, el hash y el salt se guardaron), que la cuota usada sigue igual, que el username sigue ocupado, que el usuario eliminado sigue sin existir y que su id no se reutiliza al registrar otra vez su mismo nombre.

**Por qué bloques con llaves.** El primer bloque crea el repositorio y al salir lo destruye, lo que cierra los cuatro archivos. Es lo mismo que hace un proceso que termina: sin un objeto vivo, los datos solo pueden venir del disco.

**Pruebas:** 1 nueva; `ctest` queda en 192.

### U4.2 — Errores uniformes (`ApiErrors`)

**Qué hace.** Toda respuesta de error tiene la forma de `SPEC.md` 4.1: `{"error":"codigo_estable","message":"texto"}`. Los controladores nunca escriben errores: lanzan una excepción, y `ApiServer` la entrega a `ApiErrors::WriteException` mediante el `set_exception_handler` de httplib. Como solo existe un punto de salida, ninguna ruta puede responder con otro formato ni olvidar un caso.

**Tres tipos de error.**

| Excepción | Origen | Respuesta |
|---|---|---|
| `UserError` | Regla de negocio | 400, 401, 404, 409 o 413, según la tabla `MappingOf` |
| `ApiError` | Capa HTTP: JSON inválido, sin token, sin permiso | El estado y código que trae |
| Cualquier otra, como `StorageError` | Falla del servidor | 500 `internal_error` |

**El 500 no incluye detalles.** El texto de un `StorageError` puede contener rutas de archivos, útiles para un atacante e inútiles para el cliente. Se escribe en `std::cerr` para quien opera el servidor y al cliente solo le llega un mensaje genérico. Una prueba lo verifica con una ruta de ejemplo.

**Por qué 413 para la cuota.** Lo fija `SPEC.md` 4.1. Liberar de más responde 409 porque la petición es válida, pero choca con el estado actual.

**`switch` sin `default`.** Si alguien agrega un `UserErrorCode` y olvida traducirlo, el compilador avisa, y con warnings como errores la compilación falla. El `throw` del final solo se alcanzaría con un valor fuera del enum.

**Errores que genera httplib.** Una ruta inexistente o un cuerpo demasiado grande no pasan por un controlador: llegan con el estado puesto y sin cuerpo. `FillEmptyBody` les da el mismo formato y respeta los cuerpos que ya existen, porque httplib lo llama para todo estado ≥ 400.

**Pruebas:** 14 nuevas. Una por cada código de `UserErrorCode` (9), `ApiError`, 500 sin detalles, excepción de tipo desconocido, y los dos casos de `FillEmptyBody`. Además, la ruta inexistente ahora comprueba el JSON de error por HTTP real.

### U4.1 — `UserController`: registro y login por HTTP

**Qué hace.** Es la capa que traduce HTTP a llamadas de dominio: lee el JSON, llama a `UserService` y escribe la respuesta. No contiene reglas de negocio. `POST /api/users` responde 201 con el usuario, y `POST /api/auth/login` responde 200 con `{token, user}`.

**Cómo se arman las piezas.** `main` crea los objetos en orden de dependencia: `UserRepository` → `UserService` → `AuthService` → `UserController` → `ApiServer`. Cada uno recibe por referencia lo que usa, y C++ destruye en orden inverso, así que nadie usa un objeto ya destruido. Aquí también empieza a aplicarse el mínimo de 32 bytes de `AUTH_SECRET`: si el secreto es corto, `AuthService` lanza una excepción y el servicio no arranca.

**Validación en dos niveles.** El controlador solo revisa la **forma**: que el cuerpo sea un objeto JSON y que cada campo sea texto (`invalid_json`, `invalid_request`). El **contenido** lo valida `UserService` (`invalid_password`...). `json::parse` se llama sin excepciones porque un JSON mal escrito es un error esperado del cliente.

**Límite de 16 KB por cuerpo.** La petición válida más grande ronda los 300 bytes. Sin límite, httplib acepta cuerpos enormes y cualquiera podría llenar la memoria del servidor con peticiones que ni siquiera necesitan autenticación.

**Fechas.** El registro guarda segundos desde 1970 y `SPEC.md` pide ISO 8601 UTC, así que se convierten al responder. `std::gmtime` no sirve porque usa memoria compartida entre hilos y el servidor atiende varios a la vez. Sus versiones seguras tienen distinto nombre y orden de argumentos en Windows y Linux, y por eso hay un `#ifdef` encapsulado en `ToUtc`.

**Pruebas de verdad por HTTP.** `HttpTestServer` levanta el servidor completo en un puerto libre con archivos reales en carpeta temporal, y el cliente llama por socket. Comprueban también que ninguna respuesta contenga contraseña ni salt, y que un login fallido sea idéntico byte a byte con usuario inexistente o con contraseña incorrecta.

**Pruebas:** 11 nuevas.

### U4.3 — Rutas protegidas: token y dueño

**Qué hace.** Agrega `GET /api/users/me`, `GET`, `PUT` y `DELETE /api/users/{id}`, y `GET /api/users/{id}/quota`. Dos métodos concentran la seguridad:
- `RequireToken` lee `Authorization: Bearer <token>`, lo valida con `AuthService` y responde 401 si algo falla. La causa no se revela: da igual si falta, venció o la firma es falsa.
- `RequireOwner` hace lo anterior y además compara el id de la ruta con el del token; si no coinciden, responde 403.

**401 frente a 403.** 401 significa "no sé quién eres" y 403 "sé quién eres, pero esto no es tuyo". Por eso el token se revisa primero, y antes de leer el cuerpo: un anónimo no recibe ni siquiera errores de validación.

**El id del usuario sale de la firma.** `claims.user_id` proviene del token firmado, no de algo que el cliente pueda cambiar. Por eso `/users/me` no necesita más comprobaciones.

**Perfil público.** `SPEC.md` permite que cualquier usuario autenticado consulte `GET /users/{id}`, pero no dice qué campos. Solo el dueño recibe email y consumo; los demás reciben `{id, username}`. Sin esto, cualquier cuenta podría recorrer los ids (1, 2, 3...) y juntar todos los correos.

**`/users/me` va antes de `/users/:id`.** httplib usa la primera ruta que coincide y `me` también encaja en `:id`. Sin ese orden, `/users/me` respondería 400 `invalid_user_id`.

**`std::from_chars` para el id.** Rechaza signos, letras y números que no caben en 64 bits, sin excepciones. Se exige además que consuma todo el texto: si no, `1abc` se leería como `1`.

**Campos de cuota en el `PUT`.** Se ignoran: el controlador solo lee `username`, `email` y `password`. Una prueba envía `quotaBytes` y comprueba que no cambia.

**`Handle`.** Las rutas se registran con un puntero a método (`&UserController::GetMe`), lo que evita repetir la misma lambda en cada ruta.

**Token de un usuario eliminado.** Sigue siendo válido hasta `exp` (limitación de U3.5), pero responde 404 porque el usuario no existe. Como los ids no se reutilizan, ese token nunca puede actuar sobre una cuenta nueva.

**Pruebas:** 19 nuevas: rutas sin token, cabeceras mal formadas, firma de otro secreto, token vencido, ids inválidos, perfil público, actualización parcial, cambio de contraseña, conflictos, permisos de dueño, borrado y cuota.

### U4.1 (cierre) — Cuota entre servicios con `X-Service-Token`

**Qué hace.** `POST /api/users/{id}/quota/usage` recibe `{"deltaBytes": n}`: si `n` es positivo reserva, si es negativo libera. Solo la pueden usar otros servicios, que se identifican con el header `X-Service-Token`, cuyo valor es la variable `SERVICE_SECRET`. También pueden leer con `GET /quota` la cuota de cualquier usuario, porque streaming la consulta antes de aceptar una subida.

**Por qué no basta el JWT del usuario.** El usuario es dueño de su token. Si esta ruta lo aceptara, cualquiera podría enviar un delta negativo y borrar su propio consumo, saltándose la cuota. Por eso esta ruta ignora el JWT: una prueba comprueba que el token de Ana recibe 401 y que su consumo no cambia.

**Por qué un secreto aparte y no `AUTH_SECRET`.** Con `AUTH_SECRET` se firman tokens de usuario, y separar los secretos limita el daño: la web nunca ve `SERVICE_SECRET`, y un servicio que solo valide tokens no necesita tener acceso a la cuota. Además, un secreto directo en un header es fácil de explicar. Su debilidad es que viaja tal cual por la red, algo aceptable porque el tráfico entre servicios es interno; en un despliegue público debería ir por HTTPS.

**Comparación en tiempo constante.** `ServiceTokenVerifier` usa `ConstantTimeEquals`, igual que el hash y la firma del JWT. Primero compara los tamaños: eso revela la longitud del secreto, pero no su contenido. También exige 32 bytes al arrancar, igual que `AUTH_SECRET`.

**Leer el delta sin desbordar.** nlohmann guarda los enteros no negativos como `unsigned` y los negativos como `signed`, lo que permite usar todo el rango:
- Un número que no cabe en 64 bits llega como decimal, y `is_number_integer` lo rechaza junto con `1.5`.
- Calcular `-delta` con el mínimo de `int64` es comportamiento indefinido, porque su positivo no existe en `int64`. `Magnitude` hace la resta en `uint64`, donde dar la vuelta está definido y el resultado es correcto.

**Pruebas:** 15 nuevas. 10 de la ruta (reservar, liberar, 413, 409, extremos del rango, deltas inválidos, token de usuario, token incorrecto, usuario inexistente, lectura por servicio), 4 de `ServiceTokenVerifier` y 1 de `Config`.

### U4.4 — CORS

**Qué problema resuelve.** La web corre en `localhost:5173` y la API en `localhost:8081`. Para el navegador son orígenes distintos, y por seguridad esconde la respuesta salvo que la API autorice ese origen. CORS lo aplica el navegador: no protege de un `curl`, solo decide qué páginas pueden leer las respuestas.

**Dos partes.**
- `Access-Control-Allow-Origin` va en las **cabeceras por defecto** del servidor, así que también lo llevan los errores. Si solo estuviera en las respuestas exitosas, el navegador escondería los errores y la web no podría mostrar "el usuario ya existe".
- **Consulta previa (preflight).** Antes de una petición con `Authorization` o JSON, el navegador envía un `OPTIONS` preguntando si puede hacerla. La ruta `OPTIONS` responde 204 con los métodos y cabeceras permitidos. `Access-Control-Max-Age` evita repetir la pregunta durante 10 minutos.

**Un solo origen, nunca `*`.** Viene de `CORS_ORIGIN` y cada despliegue configura el suyo.

**`X-Service-Token` no está permitido.** Solo se usa entre servicios, así que el navegador no tiene motivo para enviarlo. Una prueba lo verifica.

**Pruebas:** 3 nuevas: origen en respuestas exitosas, origen en errores, y consulta previa.

### U4.5 — Concurrencia

**El problema.** httplib atiende cada petición en un hilo de su pool, y `UserRepository` no es segura entre hilos. Ni siquiera dos lecturas pueden ir juntas: una búsqueda mueve la posición de los archivos abiertos, y otro hilo leería de una posición ajena. Por eso se usa `std::mutex` y no `shared_mutex`: permitir lectores simultáneos sería un error, no una optimización.

**Dónde vive el candado.** En `UserService`, no en el repositorio ni en el controlador, porque es la clase que conoce las dos cosas que importan:
- **Qué debe ser atómico.** `ReserveSpace` lee, valida y escribe. Si el candado estuviera en el repositorio, cada paso estaría protegido, pero otro hilo podría entrar entre leer y escribir: dos subidas pasarían la validación y una suma se perdería. Por eso el candado cubre la operación completa.
- **Qué es lento.** El hash tarda casi un segundo. Con el candado tomado, cada login detendría a todo el servicio.

**El hash, fuera del candado.**
- `Register` calcula el hash y después toma el candado.
- `Update` también lo calcula antes. El costo es que cambiar la contraseña de un usuario inexistente gasta un hash, pero esa ruta exige ser dueño.
- `Authenticate` toma el candado solo para buscar, dentro de un bloque con llaves; al salir del bloque, `lock_guard` lo suelta (RAII). El registro leído es una copia, así que verificar la contraseña no necesita el repositorio.

**Sin riesgo de bloqueo mutuo.** Hay un único mutex y ningún método público llama a otro público con el candado tomado. Para eso `LoadUser` es privado y no lo toma: lo tiene quien lo llama. Si lo tomara, `ReserveSpace` se bloquearía contra sí mismo.

**Si algo lanza excepción.** `lock_guard` suelta el mutex al destruirse, también cuando se lanza `kQuotaExceeded`.

**Limitaciones conocidas.**
- Todo acceso al disco es serial. A la escala del proyecto basta; es candidato para las optimizaciones de V2.0.
- Con 2 máquinas en V2.0, el mutex no protege entre procesos: habrá que definir cómo se comparten los archivos.

**Pruebas:** 4 nuevas con 8 hilos: reservas simultáneas sin perder bytes, nunca pasar la cuota, un solo ganador por username, y registros con login simultáneos con los índices intactos.

**Comprobado por mutación.** Se quitó temporalmente el candado de `ReserveSpace`: el proceso de pruebas abortó por el acceso simultáneo al archivo. Las pruebas sí detectan la falta del candado.
