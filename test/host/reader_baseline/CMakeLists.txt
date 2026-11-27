cmake_minimum_required(VERSION 3.20)
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
  project(BookSyncReaderBaseline LANGUAGES C CXX)
  set(CMAKE_CXX_STANDARD 20)
  set(CMAKE_CXX_STANDARD_REQUIRED ON)
  get_filename_component(REPO_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../../.." ABSOLUTE)
  include(FetchContent)
  FetchContent_Declare(googletest GIT_REPOSITORY https://github.com/google/googletest.git
    GIT_TAG 52eb8108c5bdec04579160ae17225d66034bd723)
  set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
  set(BUILD_GMOCK OFF CACHE BOOL "" FORCE)
  FetchContent_MakeAvailable(googletest)
  add_library(crosspoint_test_common INTERFACE)
  target_include_directories(crosspoint_test_common INTERFACE ${REPO_ROOT} ${REPO_ROOT}/lib ${REPO_ROOT}/lib/MemoryBudget)
  target_compile_options(crosspoint_test_common INTERFACE -Wall -Wextra -UNDEBUG)
  find_package(EXPAT REQUIRED)
  enable_testing()
  include(GoogleTest)
  # Compile the retained production implementations through their existing fault-test harnesses.
  foreach(suite IN ITEMS reader_font_sizes adobe_font_obfuscation content_opf_parser
      chapter_html_slim_parser section_persistence koreader_xpath_resolver progress_slots
      clipping_store bookmark_store book_reading_stats_atomic daily_reading_stats
      reading_session_journal stats_storage sleep_wake_policy sleep_image_deck
      fs_helpers hal_file_write_anchor sync_files protected_paths scalable_fonts otf_fonts)
    add_subdirectory(${REPO_ROOT}/test/${suite} ${suite})
  endforeach()
endif()
find_package(Python3 REQUIRED COMPONENTS Interpreter)
add_test(NAME LegacyFixtureInventory COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/test_legacy.py)
target_sources(BookReadingStatsAtomicTest PRIVATE StatsFixtureTest.cpp)
target_compile_definitions(BookReadingStatsAtomicTest PRIVATE LEGACY_FIXTURES="${REPO_ROOT}/test/fixtures/legacy")

target_sources(ProgressSlotsTest PRIVATE ProgressFixtureTest.cpp)
target_compile_definitions(ProgressSlotsTest PRIVATE LEGACY_FIXTURES="${REPO_ROOT}/test/fixtures/legacy")

add_executable(LegacyClippingFixtureTest ClippingFixtureTest.cpp
  ${REPO_ROOT}/src/ClippingStore.cpp ${REPO_ROOT}/src/sync/SyncFiles.cpp
  ${REPO_ROOT}/src/clippings/ClippingIdentity.cpp ${REPO_ROOT}/lib/Utf8/Utf8.cpp
  ${REPO_ROOT}/lib/uzlib/src/uzlib_checksums.c)
target_include_directories(LegacyClippingFixtureTest PRIVATE ${REPO_ROOT}/test/clipping_store/stubs
  ${REPO_ROOT}/src ${REPO_ROOT}/lib/Memory ${REPO_ROOT}/lib/Serialization ${REPO_ROOT}/lib/Utf8
  ${REPO_ROOT}/lib/uzlib/src)
target_compile_definitions(LegacyClippingFixtureTest PRIVATE SIMULATOR LEGACY_FIXTURES="${REPO_ROOT}/test/fixtures/legacy")
target_link_libraries(LegacyClippingFixtureTest PRIVATE crosspoint_test_common GTest::gtest_main)
gtest_discover_tests(LegacyClippingFixtureTest)
if(NOT APPLE)
  find_package(OpenSSL REQUIRED)
  target_link_libraries(LegacyClippingFixtureTest PRIVATE OpenSSL::Crypto)
endif()
