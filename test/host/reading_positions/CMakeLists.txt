add_executable(ReadingPositionsTest ReadingPositionsTest.cpp
  ${REPO_ROOT}/src/booksync/import/ReadingPositions.cpp
  ${REPO_ROOT}/src/booksync/reader/Locator.cpp
  ${REPO_ROOT}/src/booksync/storage/Identity.cpp
  ${REPO_ROOT}/src/booksync/storage/LifecycleJournal.cpp
  ${REPO_ROOT}/src/booksync/storage/FilePublish.cpp)
target_include_directories(ReadingPositionsTest PRIVATE ../file_publish/stubs ${REPO_ROOT}/src)
target_link_libraries(ReadingPositionsTest PRIVATE crosspoint_test_common GTest::gtest_main)
target_compile_definitions(ReadingPositionsTest PRIVATE REPO_ROOT="${REPO_ROOT}")
gtest_discover_tests(ReadingPositionsTest)
