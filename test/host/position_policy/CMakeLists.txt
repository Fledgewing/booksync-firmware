set(POSITION_CASES ${CMAKE_CURRENT_BINARY_DIR}/SharedCases.inc)
add_custom_command(OUTPUT ${POSITION_CASES}
  COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/generate_cases.py
    ${REPO_ROOT}/test/fixtures/position/cases.json ${POSITION_CASES}
  DEPENDS generate_cases.py ${REPO_ROOT}/test/fixtures/position/cases.json)
add_executable(PositionPolicyTest PositionPolicyTest.cpp ${POSITION_CASES}
  ${REPO_ROOT}/src/booksync/services/PositionPolicy.cpp
  ${REPO_ROOT}/src/booksync/storage/PositionStore.cpp
  ${REPO_ROOT}/src/booksync/storage/Identity.cpp
  ${REPO_ROOT}/src/booksync/storage/FilePublish.cpp
  ${REPO_ROOT}/src/booksync/services/Delivery.cpp
  ${REPO_ROOT}/src/booksync/system/ReadingGate.cpp)
target_include_directories(PositionPolicyTest PRIVATE ${CMAKE_CURRENT_BINARY_DIR} ../file_publish/stubs ${REPO_ROOT}/src)
target_link_libraries(PositionPolicyTest PRIVATE crosspoint_test_common GTest::gtest_main)
gtest_discover_tests(PositionPolicyTest)
