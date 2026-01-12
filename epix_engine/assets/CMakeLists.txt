epix_add_module(epix_assets)
target_link_libraries(epix_assets PUBLIC epix_core)
target_link_libraries(epix_assets PUBLIC stduuid)

# --- tests: auto-discover ---
# Recursively find all .cpp test sources under tests/ and create an executable + ctest entry
file(GLOB_RECURSE CORE_TEST_SOURCES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/tests/*.cpp")
foreach(TEST_SRC IN LISTS CORE_TEST_SOURCES)
  # create a unique target name based on relative path: replace separators and strip extension
  file(RELATIVE_PATH TEST_REL "${CMAKE_CURRENT_SOURCE_DIR}/tests" "${TEST_SRC}")
  string(CONCAT TEST_NAME "test_assets_" "${TEST_REL}")
  string(REPLACE "/" "_" TEST_NAME "${TEST_NAME}")
  string(REPLACE "\\" "_" TEST_NAME "${TEST_NAME}")
  string(REPLACE ".cpp" "" TEST_NAME "${TEST_NAME}")
  
  add_executable(${TEST_NAME} "${TEST_SRC}")
  target_link_libraries(${TEST_NAME} PRIVATE epix_assets)
  target_link_libraries(${TEST_NAME} PRIVATE GTest::gtest_main)
  add_test(NAME ${TEST_NAME} COMMAND ${TEST_NAME})
endforeach()