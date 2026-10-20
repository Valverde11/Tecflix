cmake_minimum_required(VERSION 3.20)
project(tecflix_api LANGUAGES CXX)

include(FetchContent)
find_package(Threads REQUIRED)

FetchContent_Declare(
    cpp_httplib
    URL https://github.com/yhirose/cpp-httplib/archive/refs/tags/v0.18.3.tar.gz
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)
FetchContent_MakeAvailable(cpp_httplib)

add_executable(tecflix-api src/main.cpp)
target_compile_features(tecflix-api PRIVATE cxx_std_17)
target_link_libraries(tecflix-api PRIVATE httplib::httplib Threads::Threads)

if(MSVC)
    target_compile_options(tecflix-api PRIVATE /W4 /O2)
else()
    target_compile_options(tecflix-api PRIVATE -Wall -Wextra -Wpedantic -O2)
endif()