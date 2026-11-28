add_executable(BookSyncReminderGateTest ReminderGateTest.cpp ReminderRetryTest.cpp ReminderFrameTest.cpp
  ${REPO_ROOT}/src/booksync/system/ReadingGate.cpp
  ${REPO_ROOT}/src/booksync/services/Reminders.cpp
  ${REPO_ROOT}/src/booksync/services/RemindersFacade.cpp
  ${REPO_ROOT}/src/booksync/services/Delivery.cpp
  ${REPO_ROOT}/src/booksync/storage/RemindersStore.cpp
  ${REPO_ROOT}/src/booksync/storage/FilePublish.cpp)
target_include_directories(BookSyncReminderGateTest PRIVATE ../file_publish/stubs ${REPO_ROOT}/src)
target_link_libraries(BookSyncReminderGateTest PRIVATE crosspoint_test_common GTest::gtest_main)
gtest_discover_tests(BookSyncReminderGateTest)
add_executable(BookSyncReminderSheetTest ReminderSheetTest.cpp ${REPO_ROOT}/src/booksync/ui/reminders/DueSheet.cpp)
target_include_directories(BookSyncReminderSheetTest PRIVATE ${REPO_ROOT}/src)
target_link_libraries(BookSyncReminderSheetTest PRIVATE crosspoint_test_common GTest::gtest_main)
gtest_discover_tests(BookSyncReminderSheetTest)
