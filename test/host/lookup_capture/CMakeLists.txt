add_executable(BookSyncLookupCaptureTest LookupCaptureTest.cpp ${REPO_ROOT}/src/booksync/services/LookupHistorySource.cpp)
target_include_directories(BookSyncLookupCaptureTest PRIVATE ${REPO_ROOT}/src ../file_publish/stubs)
target_link_libraries(BookSyncLookupCaptureTest PRIVATE crosspoint_test_common GTest::gtest_main)
gtest_discover_tests(BookSyncLookupCaptureTest)
