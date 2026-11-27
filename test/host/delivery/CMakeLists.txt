add_executable(BookSyncDeliveryTest DeliveryTest.cpp
  ${REPO_ROOT}/src/booksync/services/Delivery.cpp
  ${REPO_ROOT}/src/booksync/system/ReadingGate.cpp
  ${REPO_ROOT}/src/booksync/storage/LifecycleJournal.cpp
  ${REPO_ROOT}/src/booksync/storage/FilePublish.cpp)
target_include_directories(BookSyncDeliveryTest PRIVATE ../file_publish/stubs ${REPO_ROOT}/src)
target_link_libraries(BookSyncDeliveryTest PRIVATE crosspoint_test_common GTest::gtest_main)
gtest_discover_tests(BookSyncDeliveryTest)
