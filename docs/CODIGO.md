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
