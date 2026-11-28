add_executable(BookSyncUpdateInstallTest UpdateInstallTest.cpp
  ${REPO_ROOT}/src/booksync/system/Update.cpp
  ${REPO_ROOT}/src/booksync/system/Diagnostics.cpp
  ${REPO_ROOT}/src/booksync/system/ReadingGate.cpp
  ${REPO_ROOT}/src/booksync/system/UpdatePick.cpp
  ${REPO_ROOT}/src/booksync/system/Version.cpp
  ${REPO_ROOT}/src/booksync/system/UpdateVerify.cpp
  ${REPO_ROOT}/src/booksync/system/Ed25519Verify.cpp
  ${REPO_ROOT}/src/booksync/storage/FilePublish.cpp
  ${REPO_ROOT}/freeink-sdk/libs/network/JsonSax/src/StreamingJsonParser.cpp
  ${REPO_ROOT}/src/booksync/system/Feed.cpp)
target_include_directories(BookSyncUpdateInstallTest PRIVATE ../file_publish/stubs ../update_verify/stubs
  ${REPO_ROOT}/src ${REPO_ROOT}/lib/Memory ${REPO_ROOT}/freeink-sdk/libs/network/JsonSax/include)
target_link_libraries(BookSyncUpdateInstallTest PRIVATE crosspoint_test_common GTest::gtest_main)
gtest_discover_tests(BookSyncUpdateInstallTest)
