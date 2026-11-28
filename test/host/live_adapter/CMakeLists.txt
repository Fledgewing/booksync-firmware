add_executable(BookSyncLiveAdapterTest LiveAdapterTest.cpp
  ${REPO_ROOT}/src/booksync/server/bookorbit/LiveAdapter.cpp
  ${REPO_ROOT}/src/booksync/server/bookorbit/Auth.cpp
  ${REPO_ROOT}/src/booksync/server/bookorbit/Catalog.cpp
  ${REPO_ROOT}/src/booksync/services/Account.cpp)
target_include_directories(BookSyncLiveAdapterTest PRIVATE ../file_publish/stubs
  ${REPO_ROOT}/src ${REPO_ROOT}/lib/Memory)
target_compile_definitions(BookSyncLiveAdapterTest PRIVATE SIMULATOR)
target_link_libraries(BookSyncLiveAdapterTest PRIVATE GTest::gtest_main)
if(NOT APPLE)
  find_package(OpenSSL REQUIRED)
  target_link_libraries(BookSyncLiveAdapterTest PRIVATE OpenSSL::Crypto)
endif()
gtest_discover_tests(BookSyncLiveAdapterTest)

add_executable(BookSyncHttpTest HttpTest.cpp
  ${REPO_ROOT}/src/booksync/server/bookorbit/Http.cpp
  ${REPO_ROOT}/src/booksync/server/bookorbit/TransportTrust.cpp
  ${REPO_ROOT}/src/booksync/system/Admission.cpp
  ${REPO_ROOT}/tailnet/shim/TailnetNetPhase.cpp
  ${REPO_ROOT}/src/booksync/services/Account.cpp)
target_include_directories(BookSyncHttpTest PRIVATE ../file_publish/stubs
  ${REPO_ROOT}/freeink-sdk/libs/network/SecureNet/test/host/stubs
  ${REPO_ROOT}/freeink-sdk/libs/network/SecureNet/include
  ${REPO_ROOT}/src ${REPO_ROOT}/tailnet/shim ${REPO_ROOT}/lib/Memory)
target_link_libraries(BookSyncHttpTest PRIVATE GTest::gtest_main)
gtest_discover_tests(BookSyncHttpTest)

# Other retained host suites install simplified Memory stubs globally. This
# production HAL test needs the real fallible allocation helpers.
target_include_directories(BookSyncHttpTest BEFORE PRIVATE ${REPO_ROOT}/lib/Memory)
