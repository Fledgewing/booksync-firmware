add_executable(NativeAnnotationRecordTest AnnotationRecordTest.cpp
  ${REPO_ROOT}/src/booksync/server/bookorbit/AnnotationRecord.cpp)
target_include_directories(NativeAnnotationRecordTest PRIVATE ../file_publish/stubs ${REPO_ROOT}/src)
target_link_libraries(NativeAnnotationRecordTest PRIVATE crosspoint_test_common GTest::gtest_main)
gtest_discover_tests(NativeAnnotationRecordTest)
add_test(NAME NativeAnnotationRecoveryReader COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/recovery_test.py)
add_executable(NativeAnnotationSpoolTest AnnotationSpoolTest.cpp
  ${REPO_ROOT}/src/booksync/server/bookorbit/AnnotationSpool.cpp
  ${REPO_ROOT}/src/booksync/server/bookorbit/AnnotationRecord.cpp
  ${REPO_ROOT}/src/booksync/storage/Identity.cpp
  ${REPO_ROOT}/src/booksync/storage/FilePublish.cpp
  ${REPO_ROOT}/src/booksync/storage/LifecycleJournal.cpp
  ${REPO_ROOT}/src/booksync/reader/Capture.cpp
  ${REPO_ROOT}/src/booksync/reader/CaptureBinding.cpp)
target_include_directories(NativeAnnotationSpoolTest BEFORE PRIVATE ${REPO_ROOT}/lib/Memory)
target_include_directories(NativeAnnotationSpoolTest PRIVATE ../file_publish/stubs ${REPO_ROOT}/src)
target_link_libraries(NativeAnnotationSpoolTest PRIVATE crosspoint_test_common GTest::gtest_main)
gtest_discover_tests(NativeAnnotationSpoolTest)
