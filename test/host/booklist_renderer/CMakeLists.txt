add_executable(BookSyncBookListRendererTest BookListRendererTest.cpp ArtworkCacheTest.cpp BookListHostTest.cpp
  ${REPO_ROOT}/src/booksync/ui/WidgetHost.cpp
  ${REPO_ROOT}/src/booksync/ui/widgets/Continue.cpp
  ${REPO_ROOT}/src/booksync/ui/widgets/Browse.cpp
  ${REPO_ROOT}/src/booksync/ui/widgets/Shelf.cpp)
target_include_directories(BookSyncBookListRendererTest PRIVATE ../file_publish/stubs ${REPO_ROOT}/src ${REPO_ROOT}/lib/Memory)
target_link_libraries(BookSyncBookListRendererTest PRIVATE crosspoint_test_common GTest::gtest_main)
gtest_discover_tests(BookSyncBookListRendererTest)
