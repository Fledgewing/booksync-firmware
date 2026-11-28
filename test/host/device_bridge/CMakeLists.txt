add_executable(BookSyncDeviceBridgeTest DeviceBridgeTest.cpp ${REPO_ROOT}/src/booksync/services/DeviceBridge.cpp)
target_include_directories(BookSyncDeviceBridgeTest PRIVATE ${REPO_ROOT}/src ../file_publish/stubs)
target_link_libraries(BookSyncDeviceBridgeTest PRIVATE crosspoint_test_common GTest::gtest_main)
gtest_discover_tests(BookSyncDeviceBridgeTest)
