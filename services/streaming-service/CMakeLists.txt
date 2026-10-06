add_executable(streaming-api src/main.cpp)
target_link_libraries(streaming-api PRIVATE tecflix_http tecflix_build_options)
