add_executable(BookSyncReadingAnnotationsTest ReadingAnnotationsTest.cpp
  ${REPO_ROOT}/src/booksync/import/ReadingAnnotations.cpp
  ${REPO_ROOT}/src/booksync/storage/FilePublish.cpp)
target_include_directories(BookSyncReadingAnnotationsTest PRIVATE ../file_publish/stubs ${REPO_ROOT}/src)
target_compile_definitions(BookSyncReadingAnnotationsTest PRIVATE LEGACY_FIXTURES="${REPO_ROOT}/test/fixtures/legacy")
target_link_libraries(BookSyncReadingAnnotationsTest PRIVATE crosspoint_test_common GTest::gtest_main)
gtest_discover_tests(BookSyncReadingAnnotationsTest)
