# ============================================================================
# epix_sprite Library Configuration
# ============================================================================

file(GLOB_RECURSE RENDER_SOURCES CONFIGURE_DEPENDS "src/*.c" "src/*.cpp")
list(APPEND MODULE_INCLUDES ${CMAKE_CURRENT_SOURCE_DIR}/include)

add_library(epix_sprite STATIC ${RENDER_SOURCES})
epix_parse_options(epix_sprite)
target_include_directories(epix_sprite PUBLIC ${MODULE_INCLUDES})
target_link_libraries(epix_sprite PUBLIC epix_core)
target_link_libraries(epix_sprite PUBLIC epix_assets)
target_link_libraries(epix_sprite PUBLIC epix_image)
target_link_libraries(epix_sprite PUBLIC epix_transform)
target_link_libraries(epix_sprite PUBLIC epix_render)
target_link_libraries(epix_sprite PUBLIC epix_core_graph)

# C++20 Modules support (experimental)
if(EPIX_USE_MODULES)
  file(GLOB_RECURSE SPRITE_MODULE_FILES CONFIGURE_DEPENDS "module/*.cppm")
  
  add_library(epix_sprite_modules STATIC)
  target_compile_definitions(epix_sprite_modules PUBLIC EPIX_USE_MODULES)
  target_link_libraries(epix_sprite_modules PUBLIC epix_core_modules)
  target_link_libraries(epix_sprite_modules PUBLIC epix_assets_modules)
  target_link_libraries(epix_sprite_modules PUBLIC epix_image_modules)
  target_link_libraries(epix_sprite_modules PUBLIC epix_transform_modules)
  target_link_libraries(epix_sprite_modules PUBLIC epix_render_modules)
  
  if(CMAKE_VERSION VERSION_GREATER_EQUAL 3.28)
    target_sources(epix_sprite_modules
      PUBLIC
        FILE_SET CXX_MODULES FILES ${SPRITE_MODULE_FILES}
    )
  endif()
  
  target_link_libraries(epix_sprite_modules PRIVATE epix_sprite)
endif()

## --- tests: auto-discover ---
# Recursively find all .cpp test sources under tests/ and create an executable + ctest entry
file(GLOB_RECURSE TEST_SOURCES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/tests/*.cpp")
foreach(TEST_SRC IN LISTS TEST_SOURCES)
	# create a unique target name based on relative path: replace separators and strip extension
	file(RELATIVE_PATH TEST_REL "${CMAKE_CURRENT_SOURCE_DIR}/tests" "${TEST_SRC}")
	string(CONCAT TEST_NAME "test_sprite_" "${TEST_REL}")
	string(REPLACE "/" "_" TEST_NAME "${TEST_NAME}")
	string(REPLACE "\\" "_" TEST_NAME "${TEST_NAME}")
	string(REPLACE ".cpp" "" TEST_NAME "${TEST_NAME}")

	add_executable(${TEST_NAME} "${TEST_SRC}")
	target_link_libraries(${TEST_NAME} PRIVATE epix_sprite)
	add_test(NAME ${TEST_NAME} COMMAND ${TEST_NAME})
endforeach()