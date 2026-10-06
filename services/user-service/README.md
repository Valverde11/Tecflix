# API de usuarios

El primer incremento proporciona el endpoint `GET /health`. El registro de
usuarios, la persistencia binaria y el arbol B en disco se agregaran en
incrementos posteriores.

## Compilar desde la raiz

Se requiere CMake 3.20 o superior y Git. La primera configuracion descarga la
version fijada de `cpp-httplib`.

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release --target users-api
```

En Windows con Visual Studio:

```powershell
.\build\services\user-service\Release\users-api.exe
```

En generadores de una sola configuracion, como MinGW Makefiles:

```powershell
.\build\services\user-service\users-api.exe
```

Comprobar en otra terminal con `curl.exe http://127.0.0.1:8081/health`.
La respuesta esperada es:

```json
{"status":"ok","service":"users-api"}
```
