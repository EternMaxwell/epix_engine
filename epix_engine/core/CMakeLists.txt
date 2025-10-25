file(GLOB_RECURSE MODULE_SOURCES CONFIGURE_DEPENDS "src/*.c" "src/*.cpp")
list(APPEND MODULE_INCLUDES ${CMAKE_CURRENT_SOURCE_DIR}/include)

add_library(epix_core STATIC ${MODULE_SOURCES})
epix_parse_options(epix_core)
target_include_directories(epix_core PUBLIC ${MODULE_INCLUDES})
target_link_libraries(epix_core PUBLIC BSThreadPool)
target_link_libraries(epix_core PUBLIC spdlog::spdlog)

## --- tests: auto-discover ---
# Recursively find all .cpp test sources under tests/ and create an executable + ctest entry
file(GLOB_RECURSE CORE_TEST_SOURCES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/tests/*.cpp")
foreach(TEST_SRC IN LISTS CORE_TEST_SOURCES)
	# create a unique target name based on relative path: replace separators and strip extension
	file(RELATIVE_PATH TEST_REL "${CMAKE_CURRENT_SOURCE_DIR}" "${TEST_SRC}")
	string(REPLACE "/" "_" TEST_NAME "${TEST_REL}")
	string(REPLACE "\\" "_" TEST_NAME "${TEST_NAME}")
	string(REPLACE ".cpp" "" TEST_NAME "${TEST_NAME}")

	add_executable(${TEST_NAME} "${TEST_SRC}")
	target_link_libraries(${TEST_NAME} PRIVATE epix_core)
	add_test(NAME ${TEST_NAME} COMMAND ${TEST_NAME})
endforeach()
