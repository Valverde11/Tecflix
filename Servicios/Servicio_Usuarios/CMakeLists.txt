cmake_minimum_required(VERSION 3.20)
project(tecflix_user_service LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(USER_SERVICE_BUILD_TESTS "Compila las pruebas unitarias" ON)

include(cmake/Dependencies.cmake)

# Librería JWT compartida con metadata y streaming.
add_subdirectory("${CMAKE_CURRENT_LIST_DIR}/../../shared/auth"
                 "${CMAKE_CURRENT_BINARY_DIR}/shared-auth")

# Opciones de compilación comunes: todo warning se trata como error (SPEC 6.2).
add_library(user_service_warnings INTERFACE)
if(MSVC)
    target_compile_options(user_service_warnings INTERFACE /W4 /WX /permissive- /utf-8)
else()
    target_compile_options(user_service_warnings INTERFACE -Wall -Wextra -Wpedantic -Werror)
endif()

# La lógica vive en una librería para que las pruebas la usen sin pasar por main().
add_library(user_service_core
    src/api_error.cpp
    src/api_server.cpp
    src/btree/btree.cpp
    src/btree/btree_key.cpp
    src/btree/btree_node.cpp
    src/btree/page_file.cpp
    src/config.cpp
    src/domain/auth_service.cpp
    src/domain/user_service.cpp
    src/domain/user_validator.cpp
    src/environment.cpp
    src/health_controller.cpp
    src/security/base64_url.cpp
    src/security/crypto_primitives.cpp
    src/security/password_hasher.cpp
    src/security/service_token_verifier.cpp
    src/storage/binary_file.cpp
    src/storage/quota_operation_journal.cpp
    src/storage/byte_codec.cpp
    src/storage/user_data_file.cpp
    src/storage/user_record.cpp
    src/storage/user_repository.cpp
    src/user_controller.cpp
)
target_include_directories(user_service_core PUBLIC include)
target_link_libraries(user_service_core
    PUBLIC
        httplib::httplib
        nlohmann_json::nlohmann_json
        tecflix_auth
    PRIVATE
        mbedcrypto
        user_service_warnings
)

add_executable(user-service src/main.cpp)
target_link_libraries(user-service PRIVATE user_service_core user_service_warnings)

if(USER_SERVICE_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
