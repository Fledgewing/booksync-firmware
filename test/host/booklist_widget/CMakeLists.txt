add_executable(BookSyncBookListTest BookListTest.cpp
  ${REPO_ROOT}/src/booksync/services/widgets/BookList.cpp)
target_include_directories(BookSyncBookListTest PRIVATE ../file_publish/stubs ${REPO_ROOT}/src)
target_link_libraries(BookSyncBookListTest PRIVATE crosspoint_test_common GTest::gtest_main)
gtest_discover_tests(BookSyncBookListTest)
