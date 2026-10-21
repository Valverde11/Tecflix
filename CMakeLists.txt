cmake_minimum_required(VERSION 3.20)
project(tecflix_api LANGUAGES CXX)

find_package(Threads REQUIRED)

add_library(cpp-httplib INTERFACE)
target_include_directories(cpp-httplib SYSTEM INTERFACE
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/cpp-httplib
)
add_library(httplib::httplib ALIAS cpp-httplib)

add_executable(tecflix-api
    src/main.cpp
    src/VideoService.cpp
    src/FfmpegTranscoder.cpp
)
target_include_directories(tecflix-api PRIVATE include)
target_compile_features(tecflix-api PRIVATE cxx_std_17)
target_link_libraries(tecflix-api PRIVATE httplib::httplib Threads::Threads)

if(MSVC)
    target_compile_options(tecflix-api PRIVATE /W4 /O2)
else()
    target_compile_options(tecflix-api PRIVATE -Wall -Wextra -Wpedantic -O2)
endif()