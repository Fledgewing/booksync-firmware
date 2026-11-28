add_executable(BookSyncCatalogSurfaceTest CatalogSurfaceTest.cpp
  ${REPO_ROOT}/src/booksync/ui/Catalog.cpp)
target_include_directories(BookSyncCatalogSurfaceTest PRIVATE ${REPO_ROOT}/src)
target_link_libraries(BookSyncCatalogSurfaceTest PRIVATE crosspoint_test_common GTest::gtest_main)
gtest_discover_tests(BookSyncCatalogSurfaceTest)

add_test(NAME BookSyncCatalogGeometryGuard COMMAND ${Python3_EXECUTABLE} ${REPO_ROOT}/test/simulator/catalog_geometry.py --self-test)
