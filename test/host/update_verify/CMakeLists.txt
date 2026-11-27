add_executable(BookSyncUpdateTest UpdateTest.cpp
  ${REPO_ROOT}/src/booksync/system/Feed.cpp
  ${REPO_ROOT}/src/booksync/system/UpdateVerify.cpp
  ${REPO_ROOT}/src/booksync/system/Ed25519Verify.cpp
  ${REPO_ROOT}/freeink-sdk/libs/network/JsonSax/src/StreamingJsonParser.cpp)
target_include_directories(BookSyncUpdateTest PRIVATE
  ${REPO_ROOT}/src ${REPO_ROOT}/lib/Memory ../file_publish/stubs
  ${REPO_ROOT}/freeink-sdk/libs/network/JsonSax/include)
target_link_libraries(BookSyncUpdateTest PRIVATE crosspoint_test_common GTest::gtest_main)
gtest_discover_tests(BookSyncUpdateTest)

foreach(suite update_signature ota_signature)
  add_executable(BookSync_${suite} ${suite}_test.cpp
    ${REPO_ROOT}/src/booksync/system/UpdateVerify.cpp
    ${REPO_ROOT}/src/booksync/system/Ed25519Verify.cpp
    ${REPO_ROOT}/src/booksync/system/Feed.cpp
    ${REPO_ROOT}/freeink-sdk/libs/network/JsonSax/src/StreamingJsonParser.cpp)
  target_include_directories(BookSync_${suite} PRIVATE
    ${REPO_ROOT}/src ${REPO_ROOT}/lib/Memory ../file_publish/stubs
    ${arduinojson_SOURCE_DIR}/src ${REPO_ROOT}/freeink-sdk/libs/network/JsonSax/include)
  target_compile_features(BookSync_${suite} PRIVATE cxx_std_20)
  add_test(NAME BookSync_${suite} COMMAND BookSync_${suite} ${REPO_ROOT}/test/fixtures/update)
endforeach()
