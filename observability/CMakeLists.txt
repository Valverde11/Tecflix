cmake_minimum_required(VERSION 3.20)
project(TecflixObservability LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
include(CTest)            # primero: define BUILD_TESTING
include(FetchContent)
FetchContent_Declare(httplib
  GIT_REPOSITORY https://github.com/yhirose/cpp-httplib.git
  GIT_TAG v0.18.3 GIT_SHALLOW TRUE)
FetchContent_MakeAvailable(httplib)
add_subdirectory(telemetry)
add_subdirectory(sample-service)

if(BUILD_TESTING)
FetchContent_Declare(googletest
GIT_REPOSITORY https://github.com/google/googletest.git
GIT_TAG v1.15.2 GIT_SHALLOW TRUE)
set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)
include(GoogleTest)

add_executable(telemetry_test tests/telemetry_test.cpp)
target_link_libraries(telemetry_test PRIVATE tecflix_telemetry GTest::gtest_main)
gtest_discover_tests(telemetry_test)
add_executable(instrument_test tests/instrument_test.cpp)
target_link_libraries(instrument_test PRIVATE tecflix_telemetry_httplib GTest::gtest_main)
gtest_discover_tests(instrument_test)
  if (TECFLIX_ENABLE_TELEMETRY)

    add_executable(telemetry_export_test tests/telemetry_export_test.cpp)
    target_link_libraries(telemetry_export_test PRIVATE
      tecflix_telemetry httplib::httplib GTest::gtest_main)
    gtest_discover_tests(telemetry_export_test)
  endif()
endif()