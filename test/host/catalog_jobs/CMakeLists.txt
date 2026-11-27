add_executable(CatalogJobsTest CatalogJobsTest.cpp
  ${REPO_ROOT}/src/booksync/services/CatalogJobs.cpp
  ${REPO_ROOT}/src/booksync/services/CatalogSource.cpp
  ${REPO_ROOT}/src/booksync/services/Delivery.cpp
  ${REPO_ROOT}/src/booksync/services/ReadModel.cpp
  ${REPO_ROOT}/src/booksync/storage/ProjectionStore.cpp
  ${REPO_ROOT}/src/booksync/storage/Index.cpp
  ${REPO_ROOT}/src/booksync/storage/Identity.cpp
  ${REPO_ROOT}/src/booksync/storage/FilePublish.cpp
  ${REPO_ROOT}/src/booksync/system/Admission.cpp
  ${REPO_ROOT}/tailnet/shim/TailnetNetPhase.cpp)
target_include_directories(CatalogJobsTest PRIVATE ../file_publish/stubs ${REPO_ROOT}/src ${REPO_ROOT}/tailnet/shim)
target_link_libraries(CatalogJobsTest PRIVATE crosspoint_test_common GTest::gtest_main)
gtest_discover_tests(CatalogJobsTest)
