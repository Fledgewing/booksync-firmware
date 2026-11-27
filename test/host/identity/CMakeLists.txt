add_executable(BookSyncIdentityTest IdentityTest.cpp ${REPO_ROOT}/src/booksync/storage/Identity.cpp)
target_include_directories(BookSyncIdentityTest PRIVATE ${REPO_ROOT}/src)
target_link_libraries(BookSyncIdentityTest PRIVATE crosspoint_test_common GTest::gtest_main)
gtest_discover_tests(BookSyncIdentityTest)
