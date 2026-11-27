add_executable(BookSyncWidgetHostTest WidgetHostTest.cpp
  ${REPO_ROOT}/src/booksync/ui/WidgetHost.cpp
  ${REPO_ROOT}/src/booksync/ui/widgets/Continue.cpp
  ${REPO_ROOT}/src/booksync/ui/widgets/Shelf.cpp
  ${REPO_ROOT}/src/booksync/ui/widgets/Browse.cpp)
target_include_directories(BookSyncWidgetHostTest PRIVATE ${REPO_ROOT}/src)
target_link_libraries(BookSyncWidgetHostTest PRIVATE crosspoint_test_common GTest::gtest_main)
gtest_discover_tests(BookSyncWidgetHostTest)
