add_executable(BookSyncCatalogTest CatalogTest.cpp
  ${REPO_ROOT}/src/booksync/server/bookorbit/Catalog.cpp
  ${REPO_ROOT}/src/booksync/services/CatalogSource.cpp
  ${REPO_ROOT}/src/booksync/services/DeviceIndex.cpp
  ${REPO_ROOT}/src/booksync/services/ReadModel.cpp
  ${REPO_ROOT}/src/booksync/services/widgets/Continue.cpp
  ${REPO_ROOT}/src/booksync/storage/Index.cpp
  ${REPO_ROOT}/src/booksync/storage/FilePublish.cpp
  ${REPO_ROOT}/src/booksync/storage/Identity.cpp)
target_include_directories(BookSyncCatalogTest PRIVATE ../file_publish/stubs ${REPO_ROOT}/src)
target_link_libraries(BookSyncCatalogTest PRIVATE crosspoint_test_common GTest::gtest_main)
gtest_discover_tests(BookSyncCatalogTest)
