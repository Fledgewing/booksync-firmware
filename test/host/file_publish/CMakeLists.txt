add_executable(FilePublishTest FilePublishTest.cpp
  ${REPO_ROOT}/src/booksync/storage/FilePublish.cpp)
target_include_directories(FilePublishTest PRIVATE stubs ${REPO_ROOT}/src)
target_link_libraries(FilePublishTest PRIVATE crosspoint_test_common GTest::gtest_main)
gtest_discover_tests(FilePublishTest)
