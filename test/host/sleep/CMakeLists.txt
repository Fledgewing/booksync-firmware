add_executable(SleepTest SleepTest.cpp
  ${REPO_ROOT}/src/booksync/services/Sleep.cpp
  ${REPO_ROOT}/src/booksync/ui/Sleep.cpp
  ${REPO_ROOT}/src/booksync/system/Admission.cpp
  ${REPO_ROOT}/tailnet/shim/TailnetNetPhase.cpp)
target_include_directories(SleepTest PRIVATE ${REPO_ROOT}/src ${REPO_ROOT}/tailnet/shim)
target_link_libraries(SleepTest PRIVATE crosspoint_test_common GTest::gtest_main)
gtest_discover_tests(SleepTest)
