add_executable(BookSyncNativePersonalTest NativePersonalTest.cpp SavedNativePassagesTest.cpp
  ${REPO_ROOT}/src/booksync/services/NativePersonalSource.cpp
  ${REPO_ROOT}/src/booksync/services/SavedNativePassages.cpp
  ${REPO_ROOT}/src/booksync/services/PersonalWorker.cpp
  ${REPO_ROOT}/src/booksync/services/ReadingStats.cpp
  ${REPO_ROOT}/src/booksync/storage/PageJournal.cpp
  ${REPO_ROOT}/src/booksync/services/widgets/Library.cpp
  ${REPO_ROOT}/src/booksync/services/widgets/Lists.cpp
  ${REPO_ROOT}/src/booksync/services/PersonalReading.cpp
  ${REPO_ROOT}/src/booksync/services/Clock.cpp
  ${REPO_ROOT}/src/booksync/storage/LifecycleJournal.cpp
  ${REPO_ROOT}/src/booksync/storage/Index.cpp
  ${REPO_ROOT}/src/booksync/storage/Identity.cpp
  ${REPO_ROOT}/src/booksync/storage/FilePublish.cpp
  ${REPO_ROOT}/lib/uzlib/src/uzlib_checksums.c)
target_include_directories(BookSyncNativePersonalTest PRIVATE ../file_publish/stubs ${REPO_ROOT}/src ${REPO_ROOT}/lib/Memory ${REPO_ROOT}/lib/uzlib/src)
target_link_libraries(BookSyncNativePersonalTest PRIVATE crosspoint_test_common GTest::gtest_main)
if(NOT APPLE)
  find_package(OpenSSL REQUIRED)
  target_link_libraries(BookSyncNativePersonalTest PRIVATE OpenSSL::Crypto)
endif()
gtest_discover_tests(BookSyncNativePersonalTest)

set_source_files_properties(${REPO_ROOT}/src/booksync/services/PersonalReading.cpp PROPERTIES COMPILE_DEFINITIONS SIMULATOR)

get_target_property(personal_library_sources BookSyncIndexTest SOURCES)
list(REMOVE_AT personal_library_sources 0)
add_executable(BookSyncPersonalLibraryTest PersonalLibraryTest.cpp ${personal_library_sources})
target_include_directories(BookSyncPersonalLibraryTest PRIVATE ../file_publish/stubs ${REPO_ROOT}/src ${REPO_ROOT}/lib/Memory ${REPO_ROOT}/lib/uzlib/src)
target_compile_definitions(BookSyncPersonalLibraryTest PRIVATE FREEINK_DEVICE_X4CLASSIC=1)
target_link_libraries(BookSyncPersonalLibraryTest PRIVATE crosspoint_test_common GTest::gtest_main)
if(NOT APPLE)
  target_link_libraries(BookSyncPersonalLibraryTest PRIVATE OpenSSL::Crypto)
endif()
gtest_discover_tests(BookSyncPersonalLibraryTest)
