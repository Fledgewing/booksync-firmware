add_executable(StatsWidgetsTest StatsWidgetsTest.cpp CalendarTest.cpp TodayPagesTest.cpp WhenRecordsTest.cpp
  ${REPO_ROOT}/src/booksync/services/widgets/When.cpp
  ${REPO_ROOT}/src/booksync/services/widgets/Records.cpp
  ${REPO_ROOT}/src/booksync/ui/widgets/When.cpp
  ${REPO_ROOT}/src/booksync/ui/widgets/Records.cpp
  ${REPO_ROOT}/src/booksync/services/widgets/Year.cpp
  ${REPO_ROOT}/src/booksync/services/widgets/Monthly.cpp
  ${REPO_ROOT}/src/booksync/services/widgets/Today.cpp
  ${REPO_ROOT}/src/booksync/services/widgets/Pages.cpp
  ${REPO_ROOT}/src/booksync/services/widgets/Week.cpp
  ${REPO_ROOT}/src/booksync/services/widgets/Streak.cpp
  ${REPO_ROOT}/src/booksync/ui/widgets/Year.cpp
  ${REPO_ROOT}/src/booksync/ui/widgets/Monthly.cpp
  ${REPO_ROOT}/src/booksync/ui/widgets/Today.cpp
  ${REPO_ROOT}/src/booksync/ui/widgets/Pages.cpp
  ${REPO_ROOT}/src/booksync/ui/widgets/Week.cpp
  ${REPO_ROOT}/src/booksync/ui/widgets/Streak.cpp)
target_include_directories(StatsWidgetsTest PRIVATE ../file_publish/stubs ${REPO_ROOT}/src)
target_link_libraries(StatsWidgetsTest PRIVATE crosspoint_test_common GTest::gtest_main)
gtest_discover_tests(StatsWidgetsTest)
