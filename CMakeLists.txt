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
)
target_include_directories(tecflix-api PRIVATE include)
target_compile_features(tecflix-api PRIVATE cxx_std_17)
target_link_libraries(tecflix-api PRIVATE httplib::httplib Threads::Threads tecflix_auth)

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
    )
    target_include_directories(streaming-auth-tests PRIVATE include)
    target_compile_features(streaming-auth-tests PRIVATE cxx_std_17)
    target_link_libraries(streaming-auth-tests PRIVATE
        httplib::httplib Threads::Threads tecflix_auth)
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
