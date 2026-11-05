add_executable(user_service_tests
    api_server_test.cpp
    binary_file_test.cpp
    btree_key_test.cpp
    btree_test.cpp
    btree_node_test.cpp
    btree_remove_test.cpp
    byte_codec_test.cpp
    config_test.cpp
    page_file_test.cpp
    user_data_file_test.cpp
    user_record_test.cpp
    username_index_test.cpp
)
target_link_libraries(user_service_tests PRIVATE
    user_service_core GTest::gtest_main user_service_warnings
)

include(GoogleTest)
gtest_discover_tests(user_service_tests)
