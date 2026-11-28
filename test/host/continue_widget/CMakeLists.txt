add_executable(BookSyncContinueWidgetTest ContinueWidgetTest.cpp CompactGridTest.cpp
  ${REPO_ROOT}/src/booksync/services/widgets/Continue.cpp
  ${REPO_ROOT}/src/booksync/ui/widgets/Continue.cpp)
target_include_directories(BookSyncContinueWidgetTest PRIVATE ../file_publish/stubs ${REPO_ROOT}/src)
target_link_libraries(BookSyncContinueWidgetTest PRIVATE crosspoint_test_common GTest::gtest_main)
gtest_discover_tests(BookSyncContinueWidgetTest)
