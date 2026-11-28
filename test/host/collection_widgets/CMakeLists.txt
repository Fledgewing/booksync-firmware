add_executable(BookSyncCollectionWidgetsTest CollectionsTest.cpp
  ${REPO_ROOT}/src/booksync/services/widgets/Library.cpp
  ${REPO_ROOT}/src/booksync/services/widgets/Lists.cpp)
target_include_directories(BookSyncCollectionWidgetsTest PRIVATE ../file_publish/stubs ${REPO_ROOT}/src)
target_link_libraries(BookSyncCollectionWidgetsTest PRIVATE crosspoint_test_common GTest::gtest_main)
gtest_discover_tests(BookSyncCollectionWidgetsTest)
