add_executable(BookSyncVersionTest VersionTest.cpp
  ${REPO_ROOT}/src/booksync/system/Version.cpp
  ${REPO_ROOT}/src/booksync/system/UpdatePick.cpp
  ${REPO_ROOT}/src/booksync/system/Feed.cpp
  ${REPO_ROOT}/src/booksync/system/UpdateVerify.cpp
  ${REPO_ROOT}/src/booksync/system/Ed25519Verify.cpp
  ${REPO_ROOT}/freeink-sdk/libs/network/JsonSax/src/StreamingJsonParser.cpp)
target_include_directories(BookSyncVersionTest PRIVATE ${REPO_ROOT}/src
  ${REPO_ROOT}/lib/Memory ../update_verify/stubs ../file_publish/stubs
  ${REPO_ROOT}/freeink-sdk/libs/network/JsonSax/include)
target_link_libraries(BookSyncVersionTest PRIVATE crosspoint_test_common GTest::gtest_main)
gtest_discover_tests(BookSyncVersionTest)

add_test(NAME BookSyncVersionPackaging COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/test_packaging.py)
