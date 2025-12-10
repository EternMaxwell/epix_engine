file(GLOB_RECURSE MODULE_SOURCES CONFIGURE_DEPENDS "src/*.c" "src/*.cpp")
list(APPEND MODULE_INCLUDES ${CMAKE_CURRENT_SOURCE_DIR}/include)

# Module interface files
file(GLOB MODULE_INTERFACES "src/modules/*.cppm")

add_library(epix_core STATIC ${MODULE_SOURCES})
epix_parse_options(epix_core)

# Add module interface files if they exist and modules are supported
if(MODULE_INTERFACES AND CMAKE_VERSION VERSION_GREATER_EQUAL "3.28")
    target_sources(epix_core
        PUBLIC
            FILE_SET CXX_MODULES FILES ${MODULE_INTERFACES}
    )
    # Enable module scanning
    set_target_properties(epix_core PROPERTIES
        CXX_SCAN_FOR_MODULES ON
    )
endif()

target_include_directories(epix_core PUBLIC ${MODULE_INCLUDES})
target_link_libraries(epix_core PUBLIC BSThreadPool)
target_link_libraries(epix_core PUBLIC spdlog::spdlog)
if (EPIX_ENABLE_TRACY)
  target_compile_definitions(epix_core PRIVATE EPIX_ENABLE_TRACY)
  target_link_libraries(epix_core PRIVATE TracyClient)
endif()

## --- tests: auto-discover ---
# Recursively find all .cpp test sources under tests/ and create an executable + ctest entry
file(GLOB_RECURSE CORE_TEST_SOURCES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/tests/*.cpp")
foreach(TEST_SRC IN LISTS CORE_TEST_SOURCES)
	# create a unique target name based on relative path: replace separators and strip extension
	file(RELATIVE_PATH TEST_REL "${CMAKE_CURRENT_SOURCE_DIR}/tests" "${TEST_SRC}")
	string(CONCAT TEST_NAME "test_core_" "${TEST_REL}")
	string(REPLACE "/" "_" TEST_NAME "${TEST_NAME}")
	string(REPLACE "\\" "_" TEST_NAME "${TEST_NAME}")
	string(REPLACE ".cpp" "" TEST_NAME "${TEST_NAME}")

	add_executable(${TEST_NAME} "${TEST_SRC}")
	target_link_libraries(${TEST_NAME} PRIVATE epix_core)
	add_test(NAME ${TEST_NAME} COMMAND ${TEST_NAME})
endforeach()
