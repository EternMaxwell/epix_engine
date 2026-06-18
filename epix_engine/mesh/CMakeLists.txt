epix_add_module(epix_mesh)
target_link_libraries(epix_mesh PUBLIC epix_core)
target_link_libraries(epix_mesh PUBLIC epix_render)
target_link_libraries(epix_mesh PUBLIC epix_core_graph)
target_link_libraries(epix_mesh PUBLIC webgpu)

epix_add_tests(mesh LINK_LIBS epix_mesh)
epix_add_examples(mesh LINK_LIBS epix_mesh epix_glfw epix_glfw_render epix_input)