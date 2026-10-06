add_executable(users-api src/main.cpp)
target_link_libraries(users-api PRIVATE tecflix_http tecflix_build_options)
