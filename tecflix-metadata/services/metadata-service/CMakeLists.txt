cmake_minimum_required(VERSION 3.20)
project(tecflix_metadata_service LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(METADATA_BUILD_TESTS "Compila las pruebas unitarias" ON)
# ON usa MongoDB (necesita mongocxx, ver vcpkg.json). OFF usa solo el repositorio
# en memoria: sirve para compilar y probar en cualquier máquina sin instalar el driver.
option(METADATA_ENABLE_MONGO "Compila el repositorio MongoDB (requiere mongocxx)" OFF)

option(TECFLIX_REAL_TELEMETRY "Exportar métricas reales de metadata a OTLP" OFF)

include(cmake/Dependencies.cmake)

if(TECFLIX_REAL_TELEMETRY)
    include(FetchContent)
    add_subdirectory(
        "${CMAKE_CURRENT_LIST_DIR}/../../../observability/telemetry"
        "${CMAKE_CURRENT_BINARY_DIR}/observability-telemetry"
    )
endif()

# Mismo validador HS256 que usa user-service.
add_subdirectory("${CMAKE_CURRENT_LIST_DIR}/../../../shared/auth"
                 "${CMAKE_CURRENT_BINARY_DIR}/shared-auth")

# Opciones de compilación comunes: todo warning se trata como error (SPEC 7).
add_library(metadata_warnings INTERFACE)
if(MSVC)
    target_compile_options(metadata_warnings INTERFACE /W4 /WX /permissive- /utf-8)
else()
    target_compile_options(metadata_warnings INTERFACE -Wall -Wextra -Wpedantic -Werror)
endif()

# La lógica vive en una librería para que las pruebas la usen sin pasar por main().
add_library(metadata_core
    src/api_server.cpp
    src/catalog_error.cpp
    src/catalog_service.cpp
    src/config.cpp
    src/environment.cpp
    src/health_controller.cpp
    src/in_memory_video_repository.cpp
    src/time_util.cpp
    src/video.cpp
    src/video_controller.cpp
    src/video_json.cpp
    src/video_validation.cpp
)
target_include_directories(metadata_core PUBLIC include)
target_include_directories(
    metadata_core PRIVATE "${CMAKE_CURRENT_LIST_DIR}/../../.."
)
target_link_libraries(metadata_core
    PUBLIC httplib::httplib nlohmann_json::nlohmann_json tecflix_auth
    PRIVATE metadata_warnings
)
# cpp-httplib 0.62 exige Windows 10 o posterior. MSVC ya apunta a la versión del SDK,
# pero MinGW (el que trae CLion) declara una versión antigua y httplib.h no compila.
# PUBLIC: el ejecutable y las pruebas también incluyen httplib.h.
if(WIN32)
    target_compile_definitions(metadata_core PUBLIC _WIN32_WINNT=0x0A00)
endif()

# El ejecutable es lo único que sabe si hay MongoDB (la fábrica de repositorios y main):
# así metadata_core, que usan las pruebas, no depende del driver.
add_executable(metadata-service src/main.cpp src/repository_factory.cpp)
target_link_libraries(metadata-service PRIVATE metadata_core metadata_warnings)

if(TECFLIX_REAL_TELEMETRY)
    target_compile_definitions(
        metadata-service PRIVATE TECFLIX_REAL_TELEMETRY=1
    )

    target_link_libraries(
        metadata-service PRIVATE tecflix_telemetry
    )
endif()

if(METADATA_ENABLE_MONGO)
    find_package(mongocxx CONFIG REQUIRED)
    # Según la versión del driver, el target se llama mongocxx::... o mongo::...
    foreach(candidate mongocxx::mongocxx_shared mongo::mongocxx_shared
                      mongocxx::mongocxx_static mongo::mongocxx_static)
        if(TARGET ${candidate})
            set(METADATA_MONGOCXX_TARGET ${candidate})
            break()
        endif()
    endforeach()
    if(NOT METADATA_MONGOCXX_TARGET)
        message(FATAL_ERROR "No se encontró ningún target de mongocxx")
    endif()

    # Librería aparte y sin -Werror: los encabezados del driver no son nuestros y
    # MSVC con /WX podría fallar por avisos que no podemos corregir.
    add_library(metadata_mongo src/mongo_video_repository.cpp)
    target_include_directories(metadata_mongo PUBLIC include)
    target_link_libraries(metadata_mongo PUBLIC metadata_core ${METADATA_MONGOCXX_TARGET})
    target_link_libraries(metadata-service PRIVATE metadata_mongo)
    target_compile_definitions(metadata-service PRIVATE METADATA_WITH_MONGO=1)
endif()

if(METADATA_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
