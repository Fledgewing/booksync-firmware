add_executable(BookSyncActivationTest ActivationTest.cpp
  ${REPO_ROOT}/src/booksync/import/Activation.cpp
  ${REPO_ROOT}/src/booksync/import/Credentials.cpp
  ${REPO_ROOT}/src/booksync/services/Account.cpp
  ${REPO_ROOT}/src/booksync/storage/FilePublish.cpp)
target_include_directories(BookSyncActivationTest PRIVATE ../file_publish/stubs ${REPO_ROOT}/src ${REPO_ROOT}/lib/Memory)
target_link_libraries(BookSyncActivationTest PRIVATE crosspoint_test_common GTest::gtest_main ArduinoJson)
target_compile_definitions(BookSyncActivationTest PRIVATE FIXTURE_ROOT="${REPO_ROOT}/test/fixtures/legacy")
gtest_discover_tests(BookSyncActivationTest)
add_test(NAME BookSyncActivationSafety COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/safety_test.py)
