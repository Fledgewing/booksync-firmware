add_executable(ReaderCaptureTest ReaderCaptureTest.cpp
  ${REPO_ROOT}/src/booksync/reader/Capture.cpp
  ${REPO_ROOT}/src/booksync/reader/Locator.cpp
  ${REPO_ROOT}/src/booksync/storage/Identity.cpp
  ${REPO_ROOT}/src/booksync/storage/LifecycleJournal.cpp
  ${REPO_ROOT}/src/booksync/storage/FilePublish.cpp)
target_include_directories(ReaderCaptureTest PRIVATE ../file_publish/stubs ${REPO_ROOT}/src)
target_link_libraries(ReaderCaptureTest PRIVATE crosspoint_test_common GTest::gtest_main)
gtest_discover_tests(ReaderCaptureTest)
target_compile_definitions(ReaderCaptureTest PRIVATE REPO_ROOT="${REPO_ROOT}")

add_test(NAME ReaderCapturePaintBoundary COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/test_paint.py)
