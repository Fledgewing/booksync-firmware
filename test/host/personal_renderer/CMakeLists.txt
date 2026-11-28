add_executable(BookSyncPersonalRendererTest PersonalRendererTest.cpp PersonalHostTest.cpp
  ${REPO_ROOT}/src/booksync/ui/WidgetHost.cpp
  ${REPO_ROOT}/src/booksync/ui/widgets/Continue.cpp
  ${REPO_ROOT}/src/booksync/ui/widgets/Browse.cpp
  ${REPO_ROOT}/src/booksync/ui/widgets/Discover.cpp
  ${REPO_ROOT}/src/booksync/ui/widgets/Recommended.cpp
  ${REPO_ROOT}/src/booksync/ui/widgets/Library.cpp
  ${REPO_ROOT}/src/booksync/ui/widgets/Lists.cpp
  ${REPO_ROOT}/src/booksync/ui/widgets/Shelf.cpp)
target_include_directories(BookSyncPersonalRendererTest PRIVATE ../file_publish/stubs ${REPO_ROOT}/src)
target_link_libraries(BookSyncPersonalRendererTest PRIVATE crosspoint_test_common GTest::gtest_main)
gtest_discover_tests(BookSyncPersonalRendererTest)
