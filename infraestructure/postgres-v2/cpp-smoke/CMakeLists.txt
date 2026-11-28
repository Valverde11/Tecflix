
cmake_minimum_required(VERSION 3.18)

project(tecflix_postgres_smoke LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(PostgreSQL REQUIRED)
find_package(Threads REQUIRED)

find_path(PQXX_INCLUDE_DIR
    NAMES pqxx/pqxx
    REQUIRED
)

find_library(PQXX_LIBRARY
    NAMES pqxx
    REQUIRED
)

add_executable(postgres-smoke main.cpp)

target_include_directories(postgres-smoke PRIVATE
    ${PQXX_INCLUDE_DIR}
)

target_link_libraries(postgres-smoke PRIVATE
    ${PQXX_LIBRARY}
    PostgreSQL::PostgreSQL
    Threads::Threads
)

target_compile_options(postgres-smoke PRIVATE
    -Wall
    -Wextra
    -Wpedantic
)

