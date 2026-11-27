add_executable(LifecycleJournalTest LifecycleJournalTest.cpp
  ${REPO_ROOT}/src/booksync/storage/LifecycleJournal.cpp
  ${REPO_ROOT}/src/booksync/storage/FilePublish.cpp)
target_include_directories(LifecycleJournalTest PRIVATE ../file_publish/stubs ${REPO_ROOT}/src)
target_link_libraries(LifecycleJournalTest PRIVATE crosspoint_test_common GTest::gtest_main)
gtest_discover_tests(LifecycleJournalTest)
