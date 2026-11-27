add_executable(LifecycleMatchTest LifecycleMatchTest.cpp
  ${REPO_ROOT}/src/booksync/server/bookorbit/LifecycleMatch.cpp
  ${REPO_ROOT}/src/booksync/server/bookorbit/Auth.cpp
  ${REPO_ROOT}/src/booksync/services/Account.cpp
  ${REPO_ROOT}/src/booksync/services/SyncOwner.cpp
  ${REPO_ROOT}/src/booksync/storage/Identity.cpp
  ${REPO_ROOT}/src/booksync/storage/LifecycleJournal.cpp
  ${REPO_ROOT}/src/booksync/storage/FilePublish.cpp)
target_include_directories(LifecycleMatchTest PRIVATE ../file_publish/stubs ${REPO_ROOT}/src)
target_link_libraries(LifecycleMatchTest PRIVATE crosspoint_test_common GTest::gtest_main)
gtest_discover_tests(LifecycleMatchTest)
set_source_files_properties(${REPO_ROOT}/src/booksync/server/bookorbit/Auth.cpp PROPERTIES COMPILE_DEFINITIONS SIMULATOR)
if(NOT APPLE)
  find_package(OpenSSL REQUIRED)
  target_link_libraries(LifecycleMatchTest PRIVATE OpenSSL::Crypto)
endif()
