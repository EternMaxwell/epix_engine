file(GLOB_RECURSE RENDER_SOURCES CONFIGURE_DEPENDS "src/render/*.c" "src/render/*.cpp")
list(APPEND MODULE_INCLUDES ${CMAKE_CURRENT_SOURCE_DIR}/include)

add_library(epix_render STATIC ${RENDER_SOURCES})
epix_parse_options(epix_render)
target_include_directories(epix_render PUBLIC ${MODULE_INCLUDES})
target_link_libraries(epix_render PUBLIC epix_core)
target_link_libraries(epix_render PUBLIC epix_window)
target_link_libraries(epix_render PUBLIC epix_glfw)
target_link_libraries(epix_render PUBLIC epix_assets)
target_link_libraries(epix_render PUBLIC epix_image)
target_link_libraries(epix_render PUBLIC epix_transform)
target_link_libraries(epix_render PUBLIC Vulkan-Headers)
target_link_libraries(epix_render PUBLIC volk)
target_link_libraries(epix_render PUBLIC nvrhi)
target_link_libraries(epix_render PUBLIC nvrhi_vk)

file(GLOB_RECURSE CORE_GRAPH_SOURCES CONFIGURE_DEPENDS "src/core_graph/*.c" "src/core_graph/*.cpp")

add_library(epix_core_graph STATIC ${CORE_GRAPH_SOURCES})
epix_parse_options(epix_core_graph)
target_include_directories(epix_core_graph PUBLIC ${MODULE_INCLUDES})
target_link_libraries(epix_core_graph PUBLIC epix_core)
target_link_libraries(epix_core_graph PUBLIC epix_render)

file(GLOB_RECURSE MESH_SOURCES CONFIGURE_DEPENDS "src/mesh/*.c" "src/mesh/*.cpp")

add_library(epix_mesh STATIC ${MESH_SOURCES})
epix_parse_options(epix_mesh)
target_include_directories(epix_mesh PUBLIC ${MODULE_INCLUDES})
target_link_libraries(epix_mesh PUBLIC epix_core)
target_link_libraries(epix_mesh PUBLIC epix_render)

## --- tests: auto-discover ---
# Recursively find all .cpp test sources under tests/ and create an executable + ctest entry
file(GLOB_RECURSE TEST_SOURCES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/tests/*.cpp")
foreach(TEST_SRC IN LISTS TEST_SOURCES)
	# create a unique target name based on relative path: replace separators and strip extension
	file(RELATIVE_PATH TEST_REL "${CMAKE_CURRENT_SOURCE_DIR}/tests" "${TEST_SRC}")
	string(CONCAT TEST_NAME "test_render_" "${TEST_REL}")
	string(REPLACE "/" "_" TEST_NAME "${TEST_NAME}")
	string(REPLACE "\\" "_" TEST_NAME "${TEST_NAME}")
	string(REPLACE ".cpp" "" TEST_NAME "${TEST_NAME}")

	add_executable(${TEST_NAME} "${TEST_SRC}")
	target_link_libraries(${TEST_NAME} PRIVATE epix_render)
	target_link_libraries(${TEST_NAME} PRIVATE epix_core_graph)
	target_link_libraries(${TEST_NAME} PRIVATE epix_mesh)
	add_test(NAME ${TEST_NAME} COMMAND ${TEST_NAME})
endforeach()