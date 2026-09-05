epix_add_module_subdir(epix_render render)
target_link_libraries(epix_render PUBLIC epix_camera)
target_link_libraries(epix_render PUBLIC epix_ecs)
target_link_libraries(epix_render PUBLIC epix_window)
target_link_libraries(epix_render PUBLIC epix_assets)
target_link_libraries(epix_render PUBLIC epix_image)
target_link_libraries(epix_render PUBLIC epix_transform)
target_link_libraries(epix_render PUBLIC epix_time)
target_link_libraries(epix_render PUBLIC epix_shader)
target_link_libraries(epix_render PUBLIC epix_task)
# target_link_libraries(epix_render PUBLIC Vulkan-Headers)
target_link_libraries(epix_render PUBLIC webgpu)

epix_add_module_subdir(epix_glfw_render glfw_render)
target_link_libraries(epix_glfw_render PUBLIC epix_ecs)
target_link_libraries(epix_glfw_render PUBLIC epix_window)
target_link_libraries(epix_glfw_render PUBLIC epix_glfw)
target_link_libraries(epix_glfw_render PUBLIC epix_render)
target_link_libraries(epix_glfw_render PUBLIC webgpu)

epix_add_module_subdir(epix_sfml_render sfml_render)
target_link_libraries(epix_sfml_render PUBLIC epix_ecs)
target_link_libraries(epix_sfml_render PUBLIC epix_window)
target_link_libraries(epix_sfml_render PUBLIC epix_sfml)
target_link_libraries(epix_sfml_render PUBLIC epix_render)
target_link_libraries(epix_sfml_render PUBLIC SFML::Window)
target_link_libraries(epix_sfml_render PUBLIC webgpu)

epix_add_module_subdir(epix_core_graph core_graph)
target_link_libraries(epix_core_graph PUBLIC epix_ecs)
target_link_libraries(epix_core_graph PUBLIC epix_transform)
target_link_libraries(epix_core_graph PUBLIC epix_render)

epix_add_module_subdir(epix_imgui imgui)
target_link_libraries(epix_imgui PUBLIC epix_ecs)
target_link_libraries(epix_imgui PUBLIC epix_render)
target_link_libraries(epix_imgui PUBLIC epix_window)
target_link_libraries(epix_imgui PUBLIC epix_glfw)
target_link_libraries(epix_imgui PUBLIC epix_glfw_render)
target_link_libraries(epix_imgui PUBLIC epix_input)
target_link_libraries(epix_imgui PUBLIC webgpu)
target_link_libraries(epix_imgui PUBLIC imgui)

epix_add_tests(render
  TESTS_DIR render/tests
  LINK_LIBS epix_render epix_ecs epix_app
)

epix_add_tests(core_graph
  TESTS_DIR core_graph/tests
  LINK_LIBS epix_core_graph
)

if(EPIX_ENABLE_EXAMPLE)
# GLFW render examples (top-level .cpp files, excluding imgui*)
epix_add_examples(render_glfw
	EXCLUDE_REGEX "imgui"
	LINK_LIBS epix_render epix_glfw_render epix_core_graph
)

# SFML render examples
epix_add_examples(render_sfml
	EXAMPLES_DIR examples/sfml
	LINK_LIBS epix_render epix_sfml_render epix_core_graph
)

# ImGui render examples
epix_add_examples(render_imgui
	GLOB_TYPE GLOB
	LINK_LIBS epix_render epix_glfw_render epix_core_graph epix_imgui
)
endif()
