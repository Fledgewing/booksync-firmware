add_executable(BookSyncLayoutEditorTest LayoutEditorTest.cpp
  ${REPO_ROOT}/src/booksync/services/LayoutEditor.cpp
  ${REPO_ROOT}/src/booksync/services/LayoutGeometry.cpp
  ${REPO_ROOT}/src/booksync/services/Layout.cpp
  ${REPO_ROOT}/src/booksync/storage/LayoutStore.cpp
  ${REPO_ROOT}/src/booksync/storage/FilePublish.cpp
  ${REPO_ROOT}/src/booksync/ui/EditorAddRemove.cpp)
target_include_directories(BookSyncLayoutEditorTest PRIVATE ../file_publish/stubs ${REPO_ROOT}/src)
target_link_libraries(BookSyncLayoutEditorTest PRIVATE crosspoint_test_common GTest::gtest_main)
gtest_discover_tests(BookSyncLayoutEditorTest)
