find_package(OpenSSL REQUIRED)
add_executable(DownloadJobsTest DownloadJobsTest.cpp
  ${REPO_ROOT}/src/booksync/server/bookorbit/Download.cpp
  ${REPO_ROOT}/src/booksync/services/DownloadJobs.cpp
  ${REPO_ROOT}/src/booksync/services/Delivery.cpp
  ${REPO_ROOT}/src/booksync/storage/Identity.cpp
  ${REPO_ROOT}/src/booksync/storage/FilePublish.cpp
  ${REPO_ROOT}/src/booksync/system/Admission.cpp
  ${REPO_ROOT}/tailnet/shim/TailnetNetPhase.cpp)
target_include_directories(DownloadJobsTest PRIVATE ../file_publish/stubs ${REPO_ROOT}/src ${REPO_ROOT}/tailnet/shim)
target_link_libraries(DownloadJobsTest PRIVATE crosspoint_test_common GTest::gtest_main OpenSSL::Crypto)
gtest_discover_tests(DownloadJobsTest)
