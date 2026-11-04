add_executable(user_service_tests
    api_server_test.cpp
    config_test.cpp
)
target_link_libraries(user_service_tests PRIVATE
    user_service_core GTest::gtest_main user_service_warnings
)

include(GoogleTest)
gtest_discover_tests(user_service_tests)
