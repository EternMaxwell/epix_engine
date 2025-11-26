file(GLOB_RECURSE TEXT_SOURCES CONFIGURE_DEPENDS "src/*.c" "src/*.cpp")
list(APPEND MODULE_INCLUDES ${CMAKE_CURRENT_SOURCE_DIR}/include)

add_library(epix_text STATIC ${TEXT_SOURCES})
epix_parse_options(epix_text)
target_include_directories(epix_text PUBLIC ${MODULE_INCLUDES})
target_link_libraries(epix_text PUBLIC epix_core)
target_link_libraries(epix_text PUBLIC epix_assets)
target_link_libraries(epix_text PUBLIC epix_transform)
target_link_libraries(epix_text PUBLIC epix_render)
target_link_libraries(epix_text PUBLIC epix_core_graph)
target_link_libraries(epix_text PUBLIC freetype)

## --- tests: auto-discover ---
# Recursively find all .cpp test sources under tests/ and create an executable + ctest entry
file(GLOB_RECURSE TEST_SOURCES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/tests/*.cpp")
foreach(TEST_SRC IN LISTS TEST_SOURCES)
	# create a unique target name based on relative path: replace separators and strip extension
	file(RELATIVE_PATH TEST_REL "${CMAKE_CURRENT_SOURCE_DIR}/tests" "${TEST_SRC}")
	string(CONCAT TEST_NAME "test_text_" "${TEST_REL}")
	string(REPLACE "/" "_" TEST_NAME "${TEST_NAME}")
	string(REPLACE "\\" "_" TEST_NAME "${TEST_NAME}")
	string(REPLACE ".cpp" "" TEST_NAME "${TEST_NAME}")

	add_executable(${TEST_NAME} "${TEST_SRC}")
	target_link_libraries(${TEST_NAME} PRIVATE epix_text)
	target_link_libraries(${TEST_NAME} PRIVATE epix_sprite)
	add_test(NAME ${TEST_NAME} COMMAND ${TEST_NAME})
endforeach()