cmake_minimum_required(VERSION 3.20)
project(tecflix_loadtest LANGUAGES CXX)

find_package(Threads REQUIRED)
include(CTest)

if(NOT TARGET nlohmann_json::nlohmann_json)
    find_package(nlohmann_json 3.11 REQUIRED)
endif()

if(NOT TARGET httplib::httplib)
    add_library(tecflix-loadtest-httplib INTERFACE)
    target_include_directories(tecflix-loadtest-httplib SYSTEM INTERFACE
        "${CMAKE_CURRENT_SOURCE_DIR}/../third_party/cpp-httplib"
    )
    add_library(httplib::httplib ALIAS tecflix-loadtest-httplib)
endif()

add_library(tecflix-loadtest-core
    src/config.cpp
    src/metrics.cpp
    src/runner.cpp
)
target_include_directories(tecflix-loadtest-core
    PUBLIC include
    PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/../third_party/cpp-httplib"
)
target_compile_features(tecflix-loadtest-core PUBLIC cxx_std_17)
target_link_libraries(tecflix-loadtest-core
    PUBLIC Threads::Threads nlohmann_json::nlohmann_json
    PRIVATE httplib::httplib
)

if(MSVC)
    target_compile_options(tecflix-loadtest-core PRIVATE /W4 /permissive-)
else()
    target_compile_options(tecflix-loadtest-core PRIVATE -Wall -Wextra -Wpedantic)
endif()

if(BUILD_TESTING)
    add_executable(tecflix-loadtest-config-tests tests/config_test.cpp)
    target_link_libraries(tecflix-loadtest-config-tests PRIVATE tecflix-loadtest-core)
    add_test(NAME TecflixLoadtestConfig COMMAND tecflix-loadtest-config-tests)

    add_executable(tecflix-loadtest-metrics-tests tests/metrics_test.cpp)
    target_link_libraries(tecflix-loadtest-metrics-tests PRIVATE tecflix-loadtest-core)
    add_test(NAME TecflixLoadtestMetrics COMMAND tecflix-loadtest-metrics-tests)

    add_executable(tecflix-loadtest-runner-tests tests/runner_test.cpp)
    target_link_libraries(tecflix-loadtest-runner-tests
        PRIVATE tecflix-loadtest-core httplib::httplib
    )
    add_test(NAME TecflixLoadtestRunner COMMAND tecflix-loadtest-runner-tests)

endif()
