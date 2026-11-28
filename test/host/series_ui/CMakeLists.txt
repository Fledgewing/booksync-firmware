add_executable(SeriesUiTest SeriesUiTest.cpp ${REPO_ROOT}/src/booksync/ui/Series.cpp)
target_include_directories(SeriesUiTest PRIVATE ${REPO_ROOT}/src)
target_link_libraries(SeriesUiTest PRIVATE crosspoint_test_common GTest::gtest_main)
gtest_discover_tests(SeriesUiTest)

find_package(OpenSSL REQUIRED)
add_executable(SeriesFlowTest SeriesFlowTest.cpp
  ${REPO_ROOT}/src/booksync/services/SeriesFlow.cpp
  ${REPO_ROOT}/src/booksync/services/SeriesPlan.cpp
  ${REPO_ROOT}/src/booksync/services/SeriesQueue.cpp
  ${REPO_ROOT}/src/booksync/services/DownloadJobs.cpp
  ${REPO_ROOT}/src/booksync/services/Delivery.cpp
  ${REPO_ROOT}/src/booksync/storage/SeriesQueueStore.cpp
  ${REPO_ROOT}/src/booksync/storage/FilePublish.cpp
  ${REPO_ROOT}/src/booksync/storage/Identity.cpp
  ${REPO_ROOT}/src/booksync/system/Admission.cpp
  ${REPO_ROOT}/tailnet/shim/TailnetNetPhase.cpp)
target_include_directories(SeriesFlowTest PRIVATE ../file_publish/stubs ${REPO_ROOT}/src ${REPO_ROOT}/tailnet/shim)
target_link_libraries(SeriesFlowTest PRIVATE crosspoint_test_common GTest::gtest_main OpenSSL::Crypto)
gtest_discover_tests(SeriesFlowTest)

add_test(NAME SeriesFlowUiStackBound COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/stack_contract.py ${CMAKE_CXX_COMPILER})
