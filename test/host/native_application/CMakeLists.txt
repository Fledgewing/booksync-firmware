find_package(OpenSSL REQUIRED)
add_executable(NativeApplicationTest SourceFileTest.cpp ProgressCaptureTest.cpp NativeProgressTest.cpp NativeStateStoreTest.cpp NativeStateTest.cpp
  ${REPO_ROOT}/src/booksync/storage/SourceFile.cpp
  ${REPO_ROOT}/src/booksync/storage/Identity.cpp
  ${REPO_ROOT}/src/booksync/reader/Capture.cpp
  ${REPO_ROOT}/src/booksync/reader/CaptureBinding.cpp
  ${REPO_ROOT}/src/booksync/server/bookorbit/ProgressCapture.cpp
  ${REPO_ROOT}/src/booksync/server/bookorbit/NativeProgress.cpp
  ${REPO_ROOT}/src/booksync/server/bookorbit/NativeCaptureGate.cpp
  ${REPO_ROOT}/src/booksync/server/bookorbit/NativeStateStore.cpp
  ${REPO_ROOT}/src/booksync/server/bookorbit/NativeState.cpp
  ${REPO_ROOT}/src/booksync/server/bookorbit/LifecycleProgress.cpp
  ${REPO_ROOT}/src/booksync/server/bookorbit/LifecycleState.cpp
  ${REPO_ROOT}/src/booksync/server/bookorbit/LifecycleMatch.cpp
  ${REPO_ROOT}/src/booksync/services/Account.cpp
  ${REPO_ROOT}/src/booksync/services/PositionPolicy.cpp
  ${REPO_ROOT}/src/booksync/storage/PositionStore.cpp
  ${REPO_ROOT}/src/booksync/storage/LifecycleJournal.cpp
  ${REPO_ROOT}/src/booksync/storage/FilePublish.cpp)
target_include_directories(NativeApplicationTest BEFORE PRIVATE ${REPO_ROOT}/lib/Memory)
target_include_directories(NativeApplicationTest PRIVATE ../file_publish/stubs ${REPO_ROOT}/src)
target_link_libraries(NativeApplicationTest PRIVATE crosspoint_test_common GTest::gtest_main OpenSSL::Crypto)
gtest_discover_tests(NativeApplicationTest)
add_test(NAME NativeApplicationStackBound COMMAND ${Python3_EXECUTABLE}
  ${CMAKE_CURRENT_SOURCE_DIR}/stack_contract.py ${CMAKE_CXX_COMPILER} ${OPENSSL_INCLUDE_DIR})
add_test(NAME NativeApplicationRecoveryReader COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/recovery_test.py)
