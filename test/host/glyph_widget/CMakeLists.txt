add_executable(BookSyncGlyphWidgetTest GlyphWidgetTest.cpp
  ${REPO_ROOT}/src/booksync/services/Clock.cpp
  ${REPO_ROOT}/src/booksync/services/widgets/Glyph.cpp
  ${REPO_ROOT}/src/booksync/services/widgets/Moon.cpp
  ${REPO_ROOT}/src/booksync/services/Layout.cpp
  ${REPO_ROOT}/src/booksync/services/LayoutEditor.cpp
  ${REPO_ROOT}/src/booksync/services/LayoutGeometry.cpp
  ${REPO_ROOT}/src/booksync/ui/WidgetHost.cpp
  ${REPO_ROOT}/src/booksync/ui/widgets/Continue.cpp
  ${REPO_ROOT}/src/booksync/ui/widgets/Shelf.cpp
  ${REPO_ROOT}/src/booksync/ui/widgets/Browse.cpp
  ${REPO_ROOT}/src/booksync/ui/widgets/Glyph.cpp)
target_include_directories(BookSyncGlyphWidgetTest PRIVATE ${REPO_ROOT}/src ${REPO_ROOT}/test/host/file_publish/stubs)
target_link_libraries(BookSyncGlyphWidgetTest PRIVATE crosspoint_test_common GTest::gtest_main)
gtest_discover_tests(BookSyncGlyphWidgetTest)
