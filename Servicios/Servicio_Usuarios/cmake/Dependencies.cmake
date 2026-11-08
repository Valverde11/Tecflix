# Dependencias externas descargadas por CMake con versiones fijas.
# Se usa FetchContent en lugar de vcpkg porque el vcpkg incluido en Visual Studio 2026
# no reconoce el toolset v145; así cada integrante compila sin configuración extra.
include(FetchContent)

# cpp-httplib: misma versión que usa streaming-service, para no tener dos en el sistema.
set(HTTPLIB_USE_OPENSSL_IF_AVAILABLE OFF CACHE BOOL "" FORCE)
set(HTTPLIB_USE_ZLIB_IF_AVAILABLE OFF CACHE BOOL "" FORCE)
set(HTTPLIB_USE_BROTLI_IF_AVAILABLE OFF CACHE BOOL "" FORCE)
set(HTTPLIB_USE_ZSTD_IF_AVAILABLE OFF CACHE BOOL "" FORCE)
FetchContent_Declare(httplib
    GIT_REPOSITORY https://github.com/yhirose/cpp-httplib.git
    GIT_TAG        v0.62.0
    GIT_SHALLOW    TRUE
)

FetchContent_Declare(nlohmann_json
    GIT_REPOSITORY https://github.com/nlohmann/json.git
    GIT_TAG        v3.12.0
    GIT_SHALLOW    TRUE
)

# Mbed TLS: PBKDF2 para contraseñas y HMAC-SHA256 para el token (rama LTS 3.6).
# Se usa solo la parte criptográfica. GEN_FILES apagado evita exigir Python, porque
# la versión publicada ya trae los archivos generados, y FATAL_WARNINGS apagado
# evita que un aviso en código ajeno rompa nuestra compilación.
set(ENABLE_PROGRAMS OFF CACHE BOOL "" FORCE)
set(ENABLE_TESTING OFF CACHE BOOL "" FORCE)
set(GEN_FILES OFF CACHE BOOL "" FORCE)
set(MBEDTLS_FATAL_WARNINGS OFF CACHE BOOL "" FORCE)
set(DISABLE_PACKAGE_CONFIG_AND_INSTALL ON CACHE BOOL "" FORCE)
FetchContent_Declare(mbedtls
    GIT_REPOSITORY https://github.com/Mbed-TLS/mbedtls.git
    GIT_TAG        mbedtls-3.6.7
    GIT_SHALLOW    TRUE
)

FetchContent_MakeAvailable(httplib nlohmann_json mbedtls)

if(USER_SERVICE_BUILD_TESTS)
    # En Windows, GoogleTest debe usar el mismo runtime de C que el resto del proyecto.
    set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
    set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
    FetchContent_Declare(googletest
        GIT_REPOSITORY https://github.com/google/googletest.git
        GIT_TAG        v1.17.0
        GIT_SHALLOW    TRUE
    )
    FetchContent_MakeAvailable(googletest)
endif()
