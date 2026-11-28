add_executable(PersonalReadingTest ReadingStatsTest.cpp PersonalReadingTest.cpp
  ${REPO_ROOT}/src/booksync/services/ReadingStats.cpp
  ${REPO_ROOT}/src/booksync/services/PersonalReading.cpp
  ${REPO_ROOT}/src/booksync/services/widgets/Library.cpp
  ${REPO_ROOT}/src/booksync/services/widgets/Lists.cpp
  ${REPO_ROOT}/src/booksync/storage/PageJournal.cpp
  ${REPO_ROOT}/src/booksync/storage/LifecycleJournal.cpp
  ${REPO_ROOT}/src/booksync/storage/FilePublish.cpp)
target_include_directories(PersonalReadingTest PRIVATE ../file_publish/stubs ${REPO_ROOT}/src)
# Only the portable digest implementation needs native selection. Storage uses
# the fault-test HAL, including its volume barrier, rather than POSIX directories.
set_source_files_properties(${REPO_ROOT}/src/booksync/services/PersonalReading.cpp
  ${REPO_ROOT}/src/booksync/services/widgets/Library.cpp
  ${REPO_ROOT}/src/booksync/services/widgets/Lists.cpp
  PROPERTIES COMPILE_DEFINITIONS SIMULATOR)
target_link_libraries(PersonalReadingTest PRIVATE crosspoint_test_common GTest::gtest_main)
if(NOT APPLE)
  find_package(OpenSSL REQUIRED)
  target_link_libraries(PersonalReadingTest PRIVATE OpenSSL::Crypto)
endif()
gtest_discover_tests(PersonalReadingTest)
