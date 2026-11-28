add_executable(QueriesTest QueriesTest.cpp
  ${REPO_ROOT}/src/booksync/services/Queries.cpp
  ${REPO_ROOT}/src/booksync/storage/Index.cpp
  ${REPO_ROOT}/src/booksync/storage/Identity.cpp
  ${REPO_ROOT}/src/booksync/storage/ProjectionStore.cpp
  ${REPO_ROOT}/src/booksync/storage/FilePublish.cpp)
target_include_directories(QueriesTest PRIVATE ../file_publish/stubs ${REPO_ROOT}/src)
target_link_libraries(QueriesTest PRIVATE crosspoint_test_common GTest::gtest_main)
gtest_discover_tests(QueriesTest)
add_test(NAME BookSyncQueryStack
  COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/stack_contract.py ${CMAKE_CXX_COMPILER})
