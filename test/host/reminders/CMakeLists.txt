add_executable(BookSyncRemindersTest RemindersTest.cpp
  ${REPO_ROOT}/src/booksync/services/Reminders.cpp
  ${REPO_ROOT}/src/booksync/services/RemindersFacade.cpp
  ${REPO_ROOT}/src/booksync/services/Delivery.cpp
  ${REPO_ROOT}/src/booksync/storage/RemindersStore.cpp
  ${REPO_ROOT}/src/booksync/system/RemindersRuntime.cpp
  ${REPO_ROOT}/src/booksync/storage/FilePublish.cpp)
target_include_directories(BookSyncRemindersTest PRIVATE ../file_publish/stubs ${REPO_ROOT}/src)
target_link_libraries(BookSyncRemindersTest PRIVATE crosspoint_test_common GTest::gtest_main)
gtest_discover_tests(BookSyncRemindersTest)
