add_executable(NativeEvidenceTest NativeEvidenceTest.cpp SourceAuthorityTest.cpp
  ${REPO_ROOT}/src/booksync/reader/Capture.cpp
  ${REPO_ROOT}/src/booksync/reader/CaptureBinding.cpp
  ${REPO_ROOT}/src/booksync/server/bookorbit/NativeEvidence.cpp
  ${REPO_ROOT}/src/booksync/server/bookorbit/LifecycleMatch.cpp
  ${REPO_ROOT}/src/booksync/services/Account.cpp
  ${REPO_ROOT}/src/booksync/storage/SourceAuthority.cpp
  ${REPO_ROOT}/src/booksync/storage/Identity.cpp
  ${REPO_ROOT}/src/booksync/storage/LifecycleJournal.cpp
  ${REPO_ROOT}/src/booksync/storage/PageJournal.cpp
  ${REPO_ROOT}/src/booksync/storage/FilePublish.cpp)
target_include_directories(NativeEvidenceTest BEFORE PRIVATE ${REPO_ROOT}/lib/Memory)
target_include_directories(NativeEvidenceTest PRIVATE ../file_publish/stubs ${REPO_ROOT}/src)
target_link_libraries(NativeEvidenceTest PRIVATE crosspoint_test_common GTest::gtest_main)
gtest_discover_tests(NativeEvidenceTest)
