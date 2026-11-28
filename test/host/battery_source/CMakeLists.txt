add_executable(BookSyncBatterySourceTest BatteryAnalysisTest.cpp BatteryCaptureTest.cpp BatteryJournalTest.cpp
  ${REPO_ROOT}/src/booksync/services/Battery.cpp
  ${REPO_ROOT}/src/booksync/services/BatteryCapture.cpp
  ${REPO_ROOT}/src/booksync/storage/BatteryJournal.cpp
  ${REPO_ROOT}/src/booksync/storage/FilePublish.cpp)
target_include_directories(BookSyncBatterySourceTest PRIVATE ../file_publish/stubs ${REPO_ROOT}/src)
target_link_libraries(BookSyncBatterySourceTest PRIVATE crosspoint_test_common GTest::gtest_main)
gtest_discover_tests(BookSyncBatterySourceTest)
