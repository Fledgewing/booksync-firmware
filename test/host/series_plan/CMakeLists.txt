add_executable(SeriesPlanTest SeriesPlanTest.cpp
  ${REPO_ROOT}/src/booksync/services/SeriesPlan.cpp)
target_include_directories(SeriesPlanTest PRIVATE ../file_publish/stubs ${REPO_ROOT}/src)
target_link_libraries(SeriesPlanTest PRIVATE crosspoint_test_common GTest::gtest_main)
gtest_discover_tests(SeriesPlanTest)
add_test(NAME SeriesPlanStackBound
  COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/stack_contract.py ${CMAKE_CXX_COMPILER})
