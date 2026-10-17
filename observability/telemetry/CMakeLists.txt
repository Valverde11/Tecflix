set(_prev "${BUILD_TESTING}")
set(BUILD_TESTING OFF CACHE BOOL "" FORCE)
set(WITH_OTLP_HTTP ON CACHE BOOL "" FORCE)
set(WITH_EXAMPLES OFF CACHE BOOL "" FORCE)
FetchContent_Declare(opentelemetry_cpp
  GIT_REPOSITORY https://github.com/open-telemetry/opentelemetry-cpp.git
  GIT_TAG v1.20.0 GIT_SHALLOW TRUE)
FetchContent_MakeAvailable(opentelemetry_cpp)
set(BUILD_TESTING "${_prev}" CACHE BOOL "" FORCE)

add_library(tecflix_telemetry STATIC src/config.cpp)
target_include_directories(tecflix_telemetry PUBLIC include)
target_link_libraries(tecflix_telemetry PUBLIC
  opentelemetry_metrics opentelemetry_resources opentelemetry_exporter_otlp_http_metric)