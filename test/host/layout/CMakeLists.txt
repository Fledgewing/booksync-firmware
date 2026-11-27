add_executable(BookSyncLayoutTest LayoutTest.cpp
  ${REPO_ROOT}/src/booksync/services/Layout.cpp
  ${REPO_ROOT}/src/booksync/storage/LayoutStore.cpp
  ${REPO_ROOT}/src/booksync/storage/FilePublish.cpp)
target_include_directories(BookSyncLayoutTest PRIVATE ../file_publish/stubs ${REPO_ROOT}/src)
target_link_libraries(BookSyncLayoutTest PRIVATE crosspoint_test_common GTest::gtest_main)
gtest_discover_tests(BookSyncLayoutTest)
