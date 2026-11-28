add_executable(BookSyncIndexTest IndexTest.cpp
  ${REPO_ROOT}/src/booksync/storage/Index.cpp
  ${REPO_ROOT}/src/booksync/storage/FilePublish.cpp
  ${REPO_ROOT}/src/booksync/storage/Identity.cpp
  ${REPO_ROOT}/src/booksync/services/DeviceIndex.cpp
  ${REPO_ROOT}/src/booksync/services/ReadModel.cpp
  ${REPO_ROOT}/src/booksync/reader/Capture.cpp
  ${REPO_ROOT}/src/booksync/reader/CaptureBinding.cpp
  ${REPO_ROOT}/src/booksync/reader/Locator.cpp
  ${REPO_ROOT}/src/booksync/storage/LifecycleJournal.cpp
  ${REPO_ROOT}/src/booksync/services/NativeHistory.cpp
  ${REPO_ROOT}/src/booksync/services/Layout.cpp
  ${REPO_ROOT}/src/booksync/services/LayoutEditor.cpp
  ${REPO_ROOT}/src/booksync/services/LayoutGeometry.cpp
  ${REPO_ROOT}/src/booksync/storage/LayoutStore.cpp
  ${REPO_ROOT}/src/booksync/services/Queries.cpp
  ${REPO_ROOT}/src/booksync/storage/ProjectionStore.cpp
  ${REPO_ROOT}/src/booksync/services/Library.cpp
  ${REPO_ROOT}/src/booksync/services/Reminders.cpp
  ${REPO_ROOT}/src/booksync/services/RemindersFacade.cpp
  ${REPO_ROOT}/src/booksync/services/SdIndexSource.cpp)
target_include_directories(BookSyncIndexTest BEFORE PRIVATE ${REPO_ROOT}/lib/Memory)
target_include_directories(BookSyncIndexTest PRIVATE ../file_publish/stubs ${REPO_ROOT}/src ${REPO_ROOT}/lib/Memory)
target_link_libraries(BookSyncIndexTest PRIVATE crosspoint_test_common GTest::gtest_main)
gtest_discover_tests(BookSyncIndexTest)
