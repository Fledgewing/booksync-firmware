add_executable(BookSyncLookupPoolTest LookupPoolTest.cpp ${REPO_ROOT}/src/booksync/services/LookupPool.cpp)
target_include_directories(BookSyncLookupPoolTest PRIVATE ${REPO_ROOT}/src ${REPO_ROOT}/lib/Memory ../file_publish/stubs)
target_link_libraries(BookSyncLookupPoolTest PRIVATE crosspoint_test_common GTest::gtest_main)
gtest_discover_tests(BookSyncLookupPoolTest)
