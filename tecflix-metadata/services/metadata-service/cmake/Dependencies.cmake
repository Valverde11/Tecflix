# Dependencias externas descargadas por CMake con versiones fijas.
# Mismas versiones que user-service (cpp-httplib 0.62.0, nlohmann/json 3.12.0, GoogleTest 1.17.0).
# Cada dependencia se declara solo si no existe ya: si algún día hay un CMakeLists raíz que
# junte los servicios, no se duplican los targets (causa típica de conflictos al integrar).
include(FetchContent)

if(NOT TARGET httplib::httplib)
    set(HTTPLIB_USE_OPENSSL_IF_AVAILABLE OFF CACHE BOOL "" FORCE)
    set(HTTPLIB_USE_ZLIB_IF_AVAILABLE OFF CACHE BOOL "" FORCE)
    set(HTTPLIB_USE_BROTLI_IF_AVAILABLE OFF CACHE BOOL "" FORCE)
    set(HTTPLIB_USE_ZSTD_IF_AVAILABLE OFF CACHE BOOL "" FORCE)
    FetchContent_Declare(httplib
        GIT_REPOSITORY https://github.com/yhirose/cpp-httplib.git
        GIT_TAG        v0.62.0
        GIT_SHALLOW    TRUE
    )
    FetchContent_MakeAvailable(httplib)
endif()

if(NOT TARGET nlohmann_json::nlohmann_json)
    FetchContent_Declare(nlohmann_json
        GIT_REPOSITORY https://github.com/nlohmann/json.git
        GIT_TAG        v3.12.0
        GIT_SHALLOW    TRUE
    )
    FetchContent_MakeAvailable(nlohmann_json)
endif()

if(METADATA_BUILD_TESTS AND NOT TARGET GTest::gtest_main)
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
