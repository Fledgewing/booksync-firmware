add_executable(SeriesQueueStoreTest SeriesQueueStoreTest.cpp
  ${REPO_ROOT}/src/booksync/storage/SeriesQueueStore.cpp
  ${REPO_ROOT}/src/booksync/storage/FilePublish.cpp)
target_include_directories(SeriesQueueStoreTest PRIVATE ../file_publish/stubs ${REPO_ROOT}/src)
target_link_libraries(SeriesQueueStoreTest PRIVATE crosspoint_test_common GTest::gtest_main)
gtest_discover_tests(SeriesQueueStoreTest)

find_package(OpenSSL REQUIRED)
add_executable(SeriesQueueTest SeriesQueueTest.cpp
  ${REPO_ROOT}/src/booksync/services/SeriesQueue.cpp
  ${REPO_ROOT}/src/booksync/services/DownloadJobs.cpp
  ${REPO_ROOT}/src/booksync/services/Delivery.cpp
  ${REPO_ROOT}/src/booksync/storage/SeriesQueueStore.cpp
  ${REPO_ROOT}/src/booksync/storage/FilePublish.cpp
  ${REPO_ROOT}/src/booksync/storage/Identity.cpp
  ${REPO_ROOT}/src/booksync/system/Admission.cpp
  ${REPO_ROOT}/tailnet/shim/TailnetNetPhase.cpp)
target_include_directories(SeriesQueueTest PRIVATE ../file_publish/stubs ${REPO_ROOT}/src ${REPO_ROOT}/tailnet/shim)
target_link_libraries(SeriesQueueTest PRIVATE crosspoint_test_common GTest::gtest_main OpenSSL::Crypto)
gtest_discover_tests(SeriesQueueTest)

add_test(NAME SeriesQueueStackBound COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/stack_contract.py ${CMAKE_CXX_COMPILER})
