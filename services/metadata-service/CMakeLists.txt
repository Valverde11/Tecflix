add_executable(metadata-api src/main.cpp)
target_link_libraries(metadata-api PRIVATE tecflix_http tecflix_build_options)
