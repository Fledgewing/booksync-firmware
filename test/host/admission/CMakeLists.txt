add_executable(AdmissionTest AdmissionTest.cpp
  ${REPO_ROOT}/src/booksync/system/Admission.cpp
  ${REPO_ROOT}/tailnet/shim/TailnetNetPhase.cpp)
target_include_directories(AdmissionTest PRIVATE ${REPO_ROOT}/src ${REPO_ROOT}/tailnet/shim)
target_link_libraries(AdmissionTest PRIVATE GTest::gtest_main)
gtest_discover_tests(AdmissionTest)
add_executable(AdmissionMemoryTest MemoryTest.cpp
  ${REPO_ROOT}/src/booksync/system/Memory.cpp ${REPO_ROOT}/tailnet/shim/TailnetTlsMem.c)
target_compile_definitions(AdmissionMemoryTest PRIVATE ARDUINO_ARCH_ESP32=1 FREEINK_FB_PSRAM=1 FREEINK_NET_WOLFSSL=1)
target_include_directories(AdmissionMemoryTest PRIVATE stubs ${REPO_ROOT}/src)
target_link_libraries(AdmissionMemoryTest PRIVATE GTest::gtest_main)
gtest_discover_tests(AdmissionMemoryTest)
add_test(NAME AdmissionStackContract COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/stack_contract.py)
add_executable(AdmissionWorkerTest WorkerTest.cpp
  ${REPO_ROOT}/src/booksync/system/Worker.cpp ${REPO_ROOT}/src/booksync/system/Admission.cpp
  ${REPO_ROOT}/tailnet/shim/TailnetNetPhase.cpp)
target_include_directories(AdmissionWorkerTest PRIVATE stubs ${REPO_ROOT}/src ${REPO_ROOT}/tailnet/shim)
target_link_libraries(AdmissionWorkerTest PRIVATE GTest::gtest_main)
gtest_discover_tests(AdmissionWorkerTest)

add_executable(AdmissionC3MemoryTest MemoryTest.cpp
  ${REPO_ROOT}/src/booksync/system/Memory.cpp ${REPO_ROOT}/tailnet/shim/TailnetTlsMem.c)
target_compile_definitions(AdmissionC3MemoryTest PRIVATE ARDUINO_ARCH_ESP32=1 FREEINK_FB_PSRAM=0)
target_include_directories(AdmissionC3MemoryTest PRIVATE stubs ${REPO_ROOT}/src)
target_link_libraries(AdmissionC3MemoryTest PRIVATE GTest::gtest_main)
gtest_discover_tests(AdmissionC3MemoryTest)
