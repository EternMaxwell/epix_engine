epix_add_module_subdir(epix_render render)
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
target_link_libraries(epix_render PUBLIC webgpu)

# epix_add_module_subdir(epix_core_graph core_graph)
# target_link_libraries(epix_core_graph PUBLIC epix_core)
# target_link_libraries(epix_core_graph PUBLIC epix_render)

# epix_add_module_subdir(epix_mesh mesh)
# target_link_libraries(epix_mesh PUBLIC epix_core)
# target_link_libraries(epix_mesh PUBLIC epix_render)

## --- tests: auto-discover ---
# Recursively find all .cpp test sources under tests/ and create an executable + ctest entry
# file(GLOB_RECURSE TEST_SOURCES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/tests/*.cpp")
# foreach(TEST_SRC IN LISTS TEST_SOURCES)
# 	# create a unique target name based on relative path: replace separators and strip extension
# 	file(RELATIVE_PATH TEST_REL "${CMAKE_CURRENT_SOURCE_DIR}/tests" "${TEST_SRC}")
# 	string(CONCAT TEST_NAME "test_render_" "${TEST_REL}")
# 	string(REPLACE "/" "_" TEST_NAME "${TEST_NAME}")
# 	string(REPLACE "\\" "_" TEST_NAME "${TEST_NAME}")
# 	string(REPLACE ".cpp" "" TEST_NAME "${TEST_NAME}")

# 	add_executable(${TEST_NAME} "${TEST_SRC}")
# 	target_link_libraries(${TEST_NAME} PRIVATE epix_render)
# 	target_link_libraries(${TEST_NAME} PRIVATE epix_core_graph)
# 	target_link_libraries(${TEST_NAME} PRIVATE epix_mesh)
# 	add_test(NAME ${TEST_NAME} COMMAND ${TEST_NAME})
# endforeach()