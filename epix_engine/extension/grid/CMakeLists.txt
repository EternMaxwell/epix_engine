epix_add_module(epix_ext_grid)
target_link_libraries(epix_ext_grid PUBLIC epix_core)

# --- tests: auto-discover ---
file(GLOB_RECURSE TEST_SOURCES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/tests/*.cpp")
foreach(TEST_SRC IN LISTS TEST_SOURCES)
    file(RELATIVE_PATH TEST_REL "${CMAKE_CURRENT_SOURCE_DIR}/tests" "${TEST_SRC}")
    string(CONCAT TEST_NAME "test_grid_" "${TEST_REL}")
    string(REPLACE "/" "_" TEST_NAME "${TEST_NAME}")
    string(REPLACE "\\" "_" TEST_NAME "${TEST_NAME}")
    string(REPLACE ".cpp" "" TEST_NAME "${TEST_NAME}")

    add_executable(${TEST_NAME} "${TEST_SRC}")
    target_link_libraries(${TEST_NAME} PRIVATE epix_ext_grid)
    target_link_libraries(${TEST_NAME} PRIVATE GTest::gtest_main)
    add_test(NAME ${TEST_NAME} COMMAND ${TEST_NAME})
endforeach()