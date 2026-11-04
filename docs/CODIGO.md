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
