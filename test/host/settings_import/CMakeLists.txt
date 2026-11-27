add_executable(SettingsImportTest SettingsImportTest.cpp
  ${REPO_ROOT}/src/booksync/import/Settings.cpp
  ${REPO_ROOT}/src/booksync/storage/FilePublish.cpp)
target_include_directories(SettingsImportTest PRIVATE stubs ../file_publish/stubs ${REPO_ROOT}/src)
target_compile_definitions(SettingsImportTest PRIVATE ARDUINO_ARCH_ESP32 BOOKSYNC_IMPORT_HOST LEGACY_FIXTURES="${REPO_ROOT}/test/fixtures/legacy")
target_link_libraries(SettingsImportTest PRIVATE crosspoint_test_common GTest::gtest_main ArduinoJson)
gtest_discover_tests(SettingsImportTest)

if(NOT APPLE)
  find_package(OpenSSL REQUIRED)
  target_link_libraries(SettingsImportTest PRIVATE OpenSSL::Crypto)
endif()
