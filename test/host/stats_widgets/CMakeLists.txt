add_executable(StatsWidgetsTest StatsWidgetsTest.cpp
  ${REPO_ROOT}/src/booksync/services/widgets/Week.cpp
  ${REPO_ROOT}/src/booksync/services/widgets/Streak.cpp
  ${REPO_ROOT}/src/booksync/ui/widgets/Week.cpp
  ${REPO_ROOT}/src/booksync/ui/widgets/Streak.cpp)
target_include_directories(StatsWidgetsTest PRIVATE ../file_publish/stubs ${REPO_ROOT}/src)
target_link_libraries(StatsWidgetsTest PRIVATE crosspoint_test_common GTest::gtest_main)
gtest_discover_tests(StatsWidgetsTest)
