add_executable(LifecycleStatsTest PageJournalTest.cpp StatsTest.cpp
  ${REPO_ROOT}/src/booksync/reader/PageCapture.cpp
  ${REPO_ROOT}/src/booksync/server/bookorbit/LifecycleStats.cpp
  ${REPO_ROOT}/src/booksync/server/bookorbit/LifecycleMatch.cpp
  ${REPO_ROOT}/src/booksync/server/bookorbit/Auth.cpp
  ${REPO_ROOT}/src/booksync/services/Account.cpp
  ${REPO_ROOT}/src/booksync/services/Finish.cpp
  ${REPO_ROOT}/src/booksync/services/SyncOwner.cpp
  ${REPO_ROOT}/src/booksync/storage/Identity.cpp
  ${REPO_ROOT}/src/booksync/storage/LifecycleJournal.cpp
  ${REPO_ROOT}/src/booksync/storage/PageJournal.cpp
  ${REPO_ROOT}/src/booksync/storage/FilePublish.cpp)
target_include_directories(LifecycleStatsTest PRIVATE ../file_publish/stubs ${REPO_ROOT}/src)
target_link_libraries(LifecycleStatsTest PRIVATE crosspoint_test_common GTest::gtest_main)
gtest_discover_tests(LifecycleStatsTest)

set_source_files_properties(${REPO_ROOT}/src/booksync/server/bookorbit/Auth.cpp PROPERTIES COMPILE_DEFINITIONS SIMULATOR)
if(NOT APPLE)
  find_package(OpenSSL REQUIRED)
  target_link_libraries(LifecycleStatsTest PRIVATE OpenSSL::Crypto)
endif()

add_test(NAME PageCapturePaintBoundary COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/test_page_paint.py)
