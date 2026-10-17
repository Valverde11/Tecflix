cmake_minimum_required(VERSION 3.14)
project(tecflix_metrics_demo LANGUAGES CXX)

include(FetchContent)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

set(BUILD_TESTING OFF CACHE BOOL "" FORCE)
set(WITH_EXAMPLES OFF CACHE BOOL "" FORCE)
set(WITH_FUNC_TESTS OFF CACHE BOOL "" FORCE)
set(WITH_BENCHMARK OFF CACHE BOOL "" FORCE)
set(WITH_OTLP_HTTP ON CACHE BOOL "" FORCE)

FetchContent_Declare(
  opentelemetry_cpp
  GIT_REPOSITORY https://github.com/open-telemetry/opentelemetry-cpp.git
  GIT_TAG v1.20.0
  GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(opentelemetry_cpp)

add_executable(tecflix_metrics_demo main.cpp)
target_compile_features(tecflix_metrics_demo PRIVATE cxx_std_17)
target_link_libraries(
  tecflix_metrics_demo
  PRIVATE
    opentelemetry_metrics
    opentelemetry_resources
    opentelemetry_exporter_otlp_http_metric
)
