cmake_minimum_required(VERSION 3.20)
project(tecflix_user_service LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

include(cmake/Dependencies.cmake)

# Opciones de compilación comunes: todo warning se trata como error (SPEC 6.2).
add_library(user_service_warnings INTERFACE)
if(MSVC)
    target_compile_options(user_service_warnings INTERFACE /W4 /WX /permissive- /utf-8)
else()
    target_compile_options(user_service_warnings INTERFACE -Wall -Wextra -Wpedantic -Werror)
endif()

add_executable(user-service src/main.cpp)
target_link_libraries(user-service PRIVATE
    httplib::httplib nlohmann_json::nlohmann_json user_service_warnings
)
