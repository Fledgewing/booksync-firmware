add_executable(BookSyncCredentialsTest CredentialsTest.cpp
  ${REPO_ROOT}/src/booksync/import/Credentials.cpp
  ${REPO_ROOT}/src/booksync/services/Account.cpp
  ${REPO_ROOT}/src/booksync/server/bookorbit/Auth.cpp
  ${REPO_ROOT}/src/booksync/storage/FilePublish.cpp)
target_include_directories(BookSyncCredentialsTest PRIVATE ../file_publish/stubs ${REPO_ROOT}/src ${REPO_ROOT}/lib/Memory)
target_link_libraries(BookSyncCredentialsTest PRIVATE crosspoint_test_common GTest::gtest_main ArduinoJson)
target_compile_definitions(BookSyncCredentialsTest PRIVATE FIXTURE_ROOT="${REPO_ROOT}/test/fixtures/legacy")
if(NOT APPLE)
  find_package(OpenSSL REQUIRED)
  target_link_libraries(BookSyncCredentialsTest PRIVATE OpenSSL::Crypto)
endif()
gtest_discover_tests(BookSyncCredentialsTest)

set_source_files_properties(${REPO_ROOT}/src/booksync/server/bookorbit/Auth.cpp PROPERTIES COMPILE_DEFINITIONS SIMULATOR)
