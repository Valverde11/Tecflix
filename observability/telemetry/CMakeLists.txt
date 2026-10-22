option(TECFLIX_ENABLE_TELEMETRY "Enviar métricas con OpenTelemetry" ON)

if(TECFLIX_ENABLE_TELEMETRY)
  set(_prev "${BUILD_TESTING}")
  set(BUILD_TESTING OFF CACHE BOOL "" FORCE)
  set(WITH_OTLP_HTTP ON CACHE BOOL "" FORCE)
  set(WITH_EXAMPLES OFF CACHE BOOL "" FORCE)
  FetchContent_Declare(opentelemetry_cpp
  GIT_REPOSITORY https://github.com/open-telemetry/opentelemetry-cpp.git
    GIT_TAG v1.20.0 GIT_SHALLOW TRUE)
  FetchContent_MakeAvailable(opentelemetry_cpp)
  set(BUILD_TESTING "${_prev}" CACHE BOOL "" FORCE)

  add_library(tecflix_telemetry STATIC src/config.cpp src/telemetry.cpp)
  target_link_libraries(tecflix_telemetry PUBLIC
    opentelemetry_metrics opentelemetry_resources opentelemetry_exporter_otlp_http_metric)
else()
  add_library(tecflix_telemetry STATIC src/config.cpp src/telemetry_noop.cpp)
endif()

target_include_directories(tecflix_telemetry PUBLIC include)
option(TECFLIX_COVERAGE "Instrumentar para cobertura (clang)" OFF)
if(TECFLIX_COVERAGE)
  target_compile_options(tecflix_telemetry PUBLIC -fprofile-instr-generate -fcoverage-mapping)
  target_link_options(tecflix_telemetry PUBLIC -fprofile-instr-generate)
endif()

if(TARGET httplib::httplib)
add_library(tecflix_telemetry_httplib INTERFACE)
target_link_libraries(tecflix_telemetry_httplib INTERFACE tecflix_telemetry httplib::httplib)
endif()