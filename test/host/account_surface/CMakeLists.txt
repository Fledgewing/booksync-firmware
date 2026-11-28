add_executable(BookSyncAccountSurfaceTest AccountSurfaceTest.cpp
  ${REPO_ROOT}/src/booksync/ui/account/ServiceChoice.cpp)
target_include_directories(BookSyncAccountSurfaceTest PRIVATE ${REPO_ROOT}/src)
target_link_libraries(BookSyncAccountSurfaceTest PRIVATE crosspoint_test_common GTest::gtest_main)
gtest_discover_tests(BookSyncAccountSurfaceTest)
