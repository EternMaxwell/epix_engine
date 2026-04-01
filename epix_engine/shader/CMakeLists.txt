epix_add_module(epix_shader)
target_link_libraries(epix_shader PUBLIC epix_core)
target_link_libraries(epix_shader PUBLIC epix_assets)
target_link_libraries(epix_shader PUBLIC webgpu)

if (EPIX_ENABLE_TEST)
# --- tests: auto-discover ---
file(GLOB_RECURSE SHADER_TEST_SOURCES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/tests/*.cpp")
foreach(TEST_SRC IN LISTS SHADER_TEST_SOURCES)
	file(RELATIVE_PATH TEST_REL "${CMAKE_CURRENT_SOURCE_DIR}/tests" "${TEST_SRC}")
	string(CONCAT TEST_NAME "test_shader_" "${TEST_REL}")
	string(REPLACE "/" "_" TEST_NAME "${TEST_NAME}")
	string(REPLACE "\\" "_" TEST_NAME "${TEST_NAME}")
	string(REPLACE ".cpp" "" TEST_NAME "${TEST_NAME}")

	add_executable(${TEST_NAME} "${TEST_SRC}")
	target_link_libraries(${TEST_NAME} PRIVATE epix_shader)
	target_link_libraries(${TEST_NAME} PRIVATE GTest::gtest_main)
	add_test(NAME ${TEST_NAME} COMMAND ${TEST_NAME})
endforeach()

# --- examples: auto-discover ---
file(GLOB_RECURSE SHADER_EXAMPLE_SOURCES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/examples/*.cpp")
foreach(EXAMPLE_SRC IN LISTS SHADER_EXAMPLE_SOURCES)
	file(RELATIVE_PATH EXAMPLE_REL "${CMAKE_CURRENT_SOURCE_DIR}/examples" "${EXAMPLE_SRC}")
	string(CONCAT EXAMPLE_NAME "example_shader_" "${EXAMPLE_REL}")
	string(REPLACE "/" "_" EXAMPLE_NAME "${EXAMPLE_NAME}")
	string(REPLACE "\\" "_" EXAMPLE_NAME "${EXAMPLE_NAME}")
	string(REPLACE ".cpp" "" EXAMPLE_NAME "${EXAMPLE_NAME}")

	add_executable(${EXAMPLE_NAME} "${EXAMPLE_SRC}")
	target_link_libraries(${EXAMPLE_NAME} PRIVATE epix_shader)
endforeach()
endif()