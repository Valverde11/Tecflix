cmake_minimum_required(VERSION 3.20)

project(Tecflix VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

include(CTest)
include(FetchContent)

FetchContent_Declare(
    cpp_httplib
    GIT_REPOSITORY https://github.com/yhirose/cpp-httplib.git
    GIT_TAG v0.18.3
    GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(cpp_httplib)

add_library(tecflix_http INTERFACE)
target_include_directories(
    tecflix_http
    INTERFACE "${PROJECT_SOURCE_DIR}/services/common/include"
)
target_link_libraries(tecflix_http INTERFACE httplib::httplib)

add_library(tecflix_build_options INTERFACE)
target_compile_options(
    tecflix_build_options
    INTERFACE
        "$<$<CXX_COMPILER_ID:MSVC>:/W4>"
        "$<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-Wall;-Wextra;-Wpedantic>"
)

add_subdirectory(services/user-service)
add_subdirectory(services/metadata-service)
add_subdirectory(services/streaming-service)

if(BUILD_TESTING)
    add_executable(health_response_test tests/unit/health_response_test.cpp)
    target_link_libraries(
        health_response_test
        PRIVATE tecflix_http tecflix_build_options
    )
    add_test(NAME health_response_test COMMAND health_response_test)
endif()

install(
    TARGETS users-api metadata-api streaming-api
    RUNTIME DESTINATION bin
)
