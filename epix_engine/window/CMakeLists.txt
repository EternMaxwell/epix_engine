# ============================================================================
# epix_window Library Configuration
# ============================================================================

file(GLOB_RECURSE MODULE_SOURCES CONFIGURE_DEPENDS "src/window/*.c" "src/window/*.cpp")
list(APPEND MODULE_INCLUDES ${CMAKE_CURRENT_SOURCE_DIR}/include)

add_library(epix_window STATIC ${MODULE_SOURCES})
epix_parse_options(epix_window)
target_include_directories(epix_window PUBLIC ${MODULE_INCLUDES})
target_link_libraries(epix_window PUBLIC epix_core)
target_link_libraries(epix_window PUBLIC epix_input)
target_link_libraries(epix_window PUBLIC epix_assets)

# C++20 Modules support (experimental)
if(EPIX_USE_MODULES)
  file(GLOB_RECURSE WINDOW_MODULE_FILES CONFIGURE_DEPENDS "module/*.cppm")
  
  add_library(epix_window_modules STATIC)
  target_compile_definitions(epix_window_modules PUBLIC EPIX_USE_MODULES)
  target_link_libraries(epix_window_modules PUBLIC epix_core_modules)
  target_link_libraries(epix_window_modules PUBLIC epix_input_modules)
  target_link_libraries(epix_window_modules PUBLIC epix_assets_modules)
  
  if(CMAKE_VERSION VERSION_GREATER_EQUAL 3.28)
    target_sources(epix_window_modules
      PUBLIC
        FILE_SET CXX_MODULES FILES ${WINDOW_MODULE_FILES}
    )
  endif()
  
  target_link_libraries(epix_window_modules PRIVATE epix_window)
endif()

file(GLOB_RECURSE GLFW_SOURCES CONFIGURE_DEPENDS "src/glfw/*.c" "src/glfw/*.cpp")
add_library(epix_glfw STATIC ${GLFW_SOURCES})
epix_parse_options(epix_glfw)
target_include_directories(epix_glfw PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(epix_glfw PUBLIC epix_core)
target_link_libraries(epix_glfw PUBLIC epix_window)
target_link_libraries(epix_glfw PUBLIC epix_input)
target_link_libraries(epix_glfw PUBLIC epix_assets)
target_link_libraries(epix_glfw PUBLIC glfw)

## --- tests: auto-discover ---
# Recursively find all .cpp test sources under tests/ and create an executable + ctest entry
file(GLOB_RECURSE CORE_TEST_SOURCES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/tests/glfw/*.cpp")
foreach(TEST_SRC IN LISTS CORE_TEST_SOURCES)
	# create a unique target name based on relative path: replace separators and strip extension
	file(RELATIVE_PATH TEST_REL "${CMAKE_CURRENT_SOURCE_DIR}/tests" "${TEST_SRC}")
	string(CONCAT TEST_NAME "test_window_" "${TEST_REL}")
	string(REPLACE "/" "_" TEST_NAME "${TEST_NAME}")
	string(REPLACE "\\" "_" TEST_NAME "${TEST_NAME}")
	string(REPLACE ".cpp" "" TEST_NAME "${TEST_NAME}")

	add_executable(${TEST_NAME} "${TEST_SRC}")
	target_link_libraries(${TEST_NAME} PRIVATE epix_window)
    target_link_libraries(${TEST_NAME} PRIVATE epix_glfw)
	add_test(NAME ${TEST_NAME} COMMAND ${TEST_NAME})
endforeach()