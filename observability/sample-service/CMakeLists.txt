add_executable(sample-service src/main.cpp)
target_link_libraries(sample-service PRIVATE tecflix_telemetry_httplib)