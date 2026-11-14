cmake_minimum_required(VERSION 3.20)
project(tecflix_api LANGUAGES CXX)

find_package(Threads REQUIRED)
include(FetchContent)

# Las mismas versiones criptográficas que los servicios de usuarios y metadata.
if(NOT TARGET nlohmann_json::nlohmann_json)
    FetchContent_Declare(nlohmann_json
        GIT_REPOSITORY https://github.com/nlohmann/json.git
        GIT_TAG v3.12.0
        GIT_SHALLOW TRUE)
    FetchContent_MakeAvailable(nlohmann_json)
endif()
if(NOT TARGET mbedcrypto)
    set(ENABLE_PROGRAMS OFF CACHE BOOL "" FORCE)
    set(ENABLE_TESTING OFF CACHE BOOL "" FORCE)
    set(GEN_FILES OFF CACHE BOOL "" FORCE)
    set(MBEDTLS_FATAL_WARNINGS OFF CACHE BOOL "" FORCE)
    set(DISABLE_PACKAGE_CONFIG_AND_INSTALL ON CACHE BOOL "" FORCE)
    FetchContent_Declare(mbedtls
        GIT_REPOSITORY https://github.com/Mbed-TLS/mbedtls.git
        GIT_TAG mbedtls-3.6.7
        GIT_SHALLOW TRUE)
    FetchContent_MakeAvailable(mbedtls)
endif()

add_subdirectory(shared/auth)

option(TECFLIX_REAL_TELEMETRY "Exportar métricas de la API real a OTLP" ON)
if(TECFLIX_REAL_TELEMETRY)
    add_subdirectory(observability/telemetry)
endif()


add_library(cpp-httplib INTERFACE)
target_include_directories(cpp-httplib SYSTEM INTERFACE
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/cpp-httplib
)
add_library(httplib::httplib ALIAS cpp-httplib)

add_executable(tecflix-api
    src/main.cpp
    src/VideoService.cpp
    src/FfmpegTranscoder.cpp
    src/QuotaClient.cpp
    src/UploadRecordStore.cpp
)
target_include_directories(tecflix-api PRIVATE include)
target_compile_features(tecflix-api PRIVATE cxx_std_17)
target_link_libraries(tecflix-api PRIVATE httplib::httplib Threads::Threads tecflix_auth nlohmann_json::nlohmann_json)
target_include_directories(tecflix-api PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
if(TECFLIX_REAL_TELEMETRY)
    target_compile_definitions(tecflix-api PRIVATE TECFLIX_REAL_TELEMETRY=1)
    target_link_libraries(tecflix-api PRIVATE tecflix_telemetry)
endif()

if(MSVC)
    target_compile_options(tecflix-api PRIVATE /W4 /O2)
else()
    target_compile_options(tecflix-api PRIVATE -Wall -Wextra -Wpedantic -O2)
endif()
# Pruebas HTTP de autenticación/autorización, independientes de FFmpeg real.
include(CTest)
if(BUILD_TESTING AND NOT WIN32)
    add_executable(streaming-auth-tests
        tests/streaming_auth_tests.cpp
        src/VideoService.cpp
        src/FfmpegTranscoder.cpp
        src/UploadRecordStore.cpp
        src/QuotaClient.cpp
    )
    target_include_directories(streaming-auth-tests PRIVATE include "${CMAKE_CURRENT_SOURCE_DIR}")
    target_compile_features(streaming-auth-tests PRIVATE cxx_std_17)
    target_link_libraries(streaming-auth-tests PRIVATE
        httplib::httplib Threads::Threads tecflix_auth nlohmann_json::nlohmann_json)
    add_test(NAME StreamingAuthHttp COMMAND streaming-auth-tests)
endif()

# Pruebas del cliente de cuota sin conexión al servicio real de usuarios.
if(BUILD_TESTING AND NOT WIN32)
    add_executable(quota-client-tests
        tests/quota_client_tests.cpp
        src/QuotaClient.cpp
    )
    target_include_directories(quota-client-tests PRIVATE include)
    target_compile_features(quota-client-tests PRIVATE cxx_std_17)
    target_link_libraries(quota-client-tests PRIVATE
        httplib::httplib Threads::Threads nlohmann_json::nlohmann_json)
    add_test(NAME QuotaClientHttp COMMAND quota-client-tests)
endif()

# Persistent upload record tests (no MongoDB and no real FFmpeg required).
if(BUILD_TESTING AND NOT WIN32)
    add_executable(upload-record-store-tests
        tests/upload_record_store_tests.cpp
        src/UploadRecordStore.cpp
    )
    target_include_directories(upload-record-store-tests PRIVATE include)
    target_compile_features(upload-record-store-tests PRIVATE cxx_std_17)
    target_link_libraries(upload-record-store-tests PRIVATE nlohmann_json::nlohmann_json)
    add_test(NAME UploadRecordStore COMMAND upload-record-store-tests)
endif()

# Quota integration tests with an in-process fake users service and fake FFmpeg.
if(BUILD_TESTING AND NOT WIN32)
    add_executable(streaming-quota-integration-tests
        tests/streaming_quota_integration_tests.cpp
        src/VideoService.cpp
        src/QuotaClient.cpp
        src/FfmpegTranscoder.cpp
        src/UploadRecordStore.cpp
    )
    target_include_directories(streaming-quota-integration-tests PRIVATE include "${CMAKE_CURRENT_SOURCE_DIR}")
    target_compile_features(streaming-quota-integration-tests PRIVATE cxx_std_17)
    target_link_libraries(streaming-quota-integration-tests PRIVATE
        httplib::httplib Threads::Threads tecflix_auth nlohmann_json::nlohmann_json)
    add_test(NAME StreamingQuotaIntegration COMMAND streaming-quota-integration-tests)
endif()
