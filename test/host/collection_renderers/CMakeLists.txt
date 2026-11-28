add_executable(BookSyncCollectionRenderersTest CollectionRenderersTest.cpp
  ${REPO_ROOT}/src/booksync/ui/widgets/Library.cpp
  ${REPO_ROOT}/src/booksync/ui/widgets/Lists.cpp)
target_include_directories(BookSyncCollectionRenderersTest PRIVATE ${REPO_ROOT}/src)
target_link_libraries(BookSyncCollectionRenderersTest PRIVATE crosspoint_test_common GTest::gtest_main)
gtest_discover_tests(BookSyncCollectionRenderersTest)
