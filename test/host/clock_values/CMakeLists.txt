add_executable(BookSyncClockValuesTest ClockValuesTest.cpp ${REPO_ROOT}/src/booksync/services/Clock.cpp)
target_include_directories(BookSyncClockValuesTest PRIVATE ${REPO_ROOT}/src)
target_link_libraries(BookSyncClockValuesTest PRIVATE crosspoint_test_common GTest::gtest_main)
gtest_discover_tests(BookSyncClockValuesTest)
