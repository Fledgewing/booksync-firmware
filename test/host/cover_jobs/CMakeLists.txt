add_executable(CoverJobsTest CoverJobsTest.cpp
  ${REPO_ROOT}/src/booksync/services/CoverJobs.cpp
  ${REPO_ROOT}/src/booksync/services/CoverDecode.cpp
  ${REPO_ROOT}/src/booksync/server/bookorbit/Cover.cpp
  ${REPO_ROOT}/src/booksync/services/Delivery.cpp
  ${REPO_ROOT}/src/booksync/system/Admission.cpp
  ${REPO_ROOT}/tailnet/shim/TailnetNetPhase.cpp)
target_compile_definitions(CoverJobsTest PRIVATE __LINUX__ memcpy_P=memcpy)
target_include_directories(CoverJobsTest PRIVATE ../file_publish/stubs ${REPO_ROOT}/src
  ${REPO_ROOT}/tailnet/shim)
target_link_libraries(CoverJobsTest PRIVATE crosspoint_test_common GTest::gtest_main jpegdec_host)
gtest_discover_tests(CoverJobsTest)
