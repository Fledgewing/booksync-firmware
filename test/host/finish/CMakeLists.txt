add_executable(FinishTest FinishTest.cpp
  ${REPO_ROOT}/src/booksync/services/Finish.cpp
  ${REPO_ROOT}/src/booksync/services/SyncOwner.cpp
  ${REPO_ROOT}/src/booksync/storage/LifecycleJournal.cpp
  ${REPO_ROOT}/src/booksync/storage/FilePublish.cpp)
target_include_directories(FinishTest PRIVATE ../file_publish/stubs ${REPO_ROOT}/src)
target_link_libraries(FinishTest PRIVATE crosspoint_test_common GTest::gtest_main)
gtest_discover_tests(FinishTest)
