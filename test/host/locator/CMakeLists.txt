target_sources(ProgressSlotsTest PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/LocatorTest.cpp
  ${REPO_ROOT}/src/booksync/reader/Locator.cpp ${REPO_ROOT}/src/booksync/storage/Identity.cpp)
target_compile_definitions(ProgressSlotsTest PRIVATE LEGACY_FIXTURES="${REPO_ROOT}/test/fixtures/legacy")
