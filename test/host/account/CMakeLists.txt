add_executable(booksync_account_test AccountTest.cpp
  ${REPO_ROOT}/src/booksync/services/Account.cpp
  ${REPO_ROOT}/src/booksync/services/Delivery.cpp
  ${REPO_ROOT}/src/booksync/server/bookorbit/Auth.cpp)
target_include_directories(booksync_account_test PRIVATE ${REPO_ROOT}/src)
target_link_libraries(booksync_account_test PRIVATE GTest::gtest_main)
gtest_discover_tests(booksync_account_test)
target_compile_definitions(booksync_account_test PRIVATE SIMULATOR)
if(NOT APPLE)
  find_package(OpenSSL REQUIRED)
  target_link_libraries(booksync_account_test PRIVATE OpenSSL::Crypto)
endif()
