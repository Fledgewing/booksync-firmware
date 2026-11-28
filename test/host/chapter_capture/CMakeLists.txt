add_executable(BookSyncChapterCaptureTest ChapterCaptureTest.cpp ${REPO_ROOT}/src/booksync/reader/ChapterCapture.cpp)
target_include_directories(BookSyncChapterCaptureTest PRIVATE ${REPO_ROOT}/src ../file_publish/stubs)
target_link_libraries(BookSyncChapterCaptureTest PRIVATE crosspoint_test_common GTest::gtest_main)
gtest_discover_tests(BookSyncChapterCaptureTest)
