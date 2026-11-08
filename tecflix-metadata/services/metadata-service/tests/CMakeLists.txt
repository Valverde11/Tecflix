add_executable(metadata_service_tests
    api_server_test.cpp
    catalog_service_test.cpp
    config_test.cpp
    in_memory_video_repository_test.cpp
    time_util_test.cpp
    video_json_test.cpp
    video_validation_test.cpp
)
target_link_libraries(metadata_service_tests PRIVATE
    metadata_core GTest::gtest_main metadata_warnings
)

include(GoogleTest)
gtest_discover_tests(metadata_service_tests)
