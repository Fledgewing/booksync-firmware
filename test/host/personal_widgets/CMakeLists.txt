add_executable(BookSyncPersonalWidgetsTest PersonalWidgetsTest.cpp
  ${REPO_ROOT}/src/booksync/services/widgets/BookList.cpp
  ${REPO_ROOT}/src/booksync/services/widgets/Library.cpp
  ${REPO_ROOT}/src/booksync/services/widgets/Lists.cpp
  ${REPO_ROOT}/src/booksync/services/widgets/PersonalWidgets.cpp
  ${REPO_ROOT}/src/booksync/services/PersonalReading.cpp
  ${REPO_ROOT}/src/booksync/services/ReadingStats.cpp
  ${REPO_ROOT}/src/booksync/storage/PageJournal.cpp
  ${REPO_ROOT}/src/booksync/storage/LifecycleJournal.cpp
  ${REPO_ROOT}/src/booksync/storage/FilePublish.cpp)
set_source_files_properties(${REPO_ROOT}/src/booksync/services/PersonalReading.cpp
  PROPERTIES COMPILE_DEFINITIONS SIMULATOR)
target_include_directories(BookSyncPersonalWidgetsTest PRIVATE ../file_publish/stubs ${REPO_ROOT}/src)
target_link_libraries(BookSyncPersonalWidgetsTest PRIVATE crosspoint_test_common GTest::gtest_main)
if(NOT APPLE)
  find_package(OpenSSL REQUIRED)
  target_link_libraries(BookSyncPersonalWidgetsTest PRIVATE OpenSSL::Crypto)
endif()
gtest_discover_tests(BookSyncPersonalWidgetsTest)
